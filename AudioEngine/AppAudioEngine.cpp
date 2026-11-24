#include "AppAudioEngine.h"

#include "Libraries.h"

namespace pnam
{
AppAudioEngine::AppAudioEngine()
{
    amp_.prepare(48000, 512);
    cab_.prepare(48000, 512);
    monoIn_.assign(512, 0.0f);
    ampOut_.assign(512, 0.0f);
    left_.assign(512, 0.0f);
    right_.assign(512, 0.0f);
}

AppAudioEngine::~AppAudioEngine()
{
    devices_.removeAudioCallback(this);
    devices_.closeAudioDevice();
    loader_.removeAllJobs(true, 10000);
}

void AppAudioEngine::initialiseAudio(const juce::XmlElement* saved)
{
    auto err = devices_.initialise(2, 2, saved, true);
    if (err.isNotEmpty())
        DBG("audio init: " << err);
    devices_.addAudioCallback(this);
}

void AppAudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* d)
{
    sampleRate_ = d->getCurrentSampleRate();
    blockSize_ = d->getCurrentBufferSizeSamples();
    const int mb = juce::jmax(blockSize_, 32);
    amp_.prepare(sampleRate_, mb);
    cab_.prepare(sampleRate_, mb);
    monoIn_.assign((size_t) mb, 0.0f);
    ampOut_.assign((size_t) mb, 0.0f);
    left_.assign((size_t) mb, 0.0f);
    right_.assign((size_t) mb, 0.0f);
}

void AppAudioEngine::audioDeviceStopped() { cab_.reset(); }

void AppAudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    const int maxN = (int) monoIn_.size();
    for (int p = 0; p < n; p += maxN)
    {
        const int m = juce::jmin(maxN, n - p);
        const int ch = inputChannel_.load(std::memory_order_relaxed);
        if (numIn > 0 && in[juce::jlimit(0, numIn - 1, ch)] != nullptr)
            juce::FloatVectorOperations::copy(monoIn_.data(), in[juce::jlimit(0, numIn - 1, ch)] + p, m);
        else
            juce::FloatVectorOperations::clear(monoIn_.data(), m);
        amp_.processBlock(monoIn_.data(), ampOut_.data(), m);
        cab_.process(ampOut_.data(), left_.data(), right_.data(), m);
        if (!stereo_.load(std::memory_order_relaxed))
            for (int i = 0; i < m; ++i)
                left_[(size_t) i] = right_[(size_t) i] = 0.5f * (left_[(size_t) i] + right_[(size_t) i]);
        float pk = 0;
        for (int i = 0; i < m; ++i)
            pk = juce::jmax(pk, std::abs(left_[(size_t) i]), std::abs(right_[(size_t) i]));
        float old = outMeterPeak_.load(std::memory_order_relaxed);
        while (pk > old && !outMeterPeak_.compare_exchange_weak(old, pk)) {}
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr)
                juce::FloatVectorOperations::copy(out[c] + p, (c % 2 == 0 ? left_ : right_).data(), m);
    }
}

void AppAudioEngine::loadModelBlocking(const juce::File& file)
{
    auto pm = loadPlayableModel(file.getFullPathName().toStdString());
    auto* raw = pm.net.get();
    auto info = pm.info;
    auto json = std::move(pm.json);
    // reset control targets to the new model's defaults before building the slot
    for (int i = 0; i < raw->knobCount(); ++i)
        amp_.setKnob(i, raw->knob(i).def);
    auto slot = amp_.makeSlot(std::unique_ptr<nam::DSP>(pm.net.release()), info, raw);
    amp_.setModel(std::move(slot));
    model_ = ModelMeta { info, std::move(json) };
    modelFile_ = file;
    amp_.collectGarbage();
}

void AppAudioEngine::loadModel(const juce::File& file, LoadCallback done)
{
    const int gen = ++generation_;
    loader_.addJob([this, file, done, gen] {
        juce::String err;
        std::unique_ptr<ModelSlot> slot;
        std::optional<ModelMeta> meta;
        std::vector<float> defaults;
        try
        {
            auto pm = loadPlayableModel(file.getFullPathName().toStdString());
            auto* raw = pm.net.get();
            for (int i = 0; i < raw->knobCount(); ++i)
                defaults.push_back(raw->knob(i).def);
            meta = ModelMeta { pm.info, std::move(pm.json) };
            slot = amp_.makeSlot(std::unique_ptr<nam::DSP>(pm.net.release()), meta->info, raw, &defaults);
        }
        catch (const std::exception& e)
        {
            err = e.what();
        }
        juce::MessageManager::callAsync([this, err, done, gen, file, defaults, s = std::shared_ptr<ModelSlot>(slot.release()), meta]() mutable {
            if (gen != generation_.load())
            {
                // superseded by a newer load
                return;
            }
            if (err.isEmpty())
            {
                for (size_t i = 0; i < defaults.size(); ++i)
                    amp_.setKnob((int) i, defaults[i]);
                // shared_ptr -> unique_ptr hand-over (we are the only owner)
                amp_.setModel(std::unique_ptr<ModelSlot>(new ModelSlot(std::move(*s))));
                model_ = std::move(meta);
                modelFile_ = file;
            }
            amp_.collectGarbage();
            if (done)
                done(err);
        });
    });
}

juce::String AppAudioEngine::loadReference(const juce::File& file)
{
    try
    {
        auto c = loadSourceCapture(file.getFullPathName().toStdString());
        auto slot = amp_.makeSlot(std::move(c.dsp), c.info, nullptr);
        amp_.setReference(std::move(slot));
        referenceFile_ = file;
        return {};
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
}

void AppAudioEngine::clearReference()
{
    amp_.setReference(nullptr);
    amp_.setABListenToReference(false);
    referenceFile_ = juce::File();
}

juce::String AppAudioEngine::loadIR(int slot, const juce::File& f)
{
    auto err = cab_.loadIR(slot, f);
    if (err.isEmpty())
    {
        auto& s = slot == 0 ? state_.irA : state_.irB;
        s.path = f.getFullPathName().toStdString();
        s.enabled = true;
        cab_.setEnabled(slot, true);
    }
    return err;
}

void AppAudioEngine::setIR(int slot, const IRSlotSettings& s)
{
    (slot == 0 ? state_.irA : state_.irB) = s;
    cab_.setEnabled(slot, s.enabled);
    cab_.setLevelDb(slot, s.levelDb);
    cab_.setPan(slot, s.pan);
    cab_.setInvert(slot, s.invert);
}

int AppAudioEngine::totalLatencySamples() const
{
    return amp_.latencySamples() + const_cast<DualIRCabinet&>(cab_).latencySamples();
}

PresetState AppAudioEngine::captureState() const
{
    PresetState s = state_;
    s.name.clear();
    if (model_)
    {
        s.modelPath = modelFile_.getFullPathName().toStdString();
        s.modelHash = hashToHex(model_->info.weightsHash);
        s.modelName = model_->info.displayName();
        s.knobNames.clear();
        s.knobValues.clear();
        for (size_t i = 0; i < model_->info.knobs.size(); ++i)
        {
            s.knobNames.push_back(model_->info.knobs[i].name);
            s.knobValues.push_back(amp_.knob((int) i));
        }
    }
    return s;
}

juce::String AppAudioEngine::applyState(const PresetState& s, std::function<void(juce::String)> done)
{
    setInputGainDb(s.inputGainDb);
    setOutputGainDb(s.outputGainDb);
    setGate(s.gateOn, s.gateThresholdDb);
    setNormalize(s.normalizeLoudness);
    setStereo(s.stereo);
    setCabEnabled(s.cabEnabled);
    juce::String irErrors;
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto& ir = slot == 0 ? s.irA : s.irB;
        if (!ir.path.empty())
        {
            juce::File f(ir.path);
            if (f.existsAsFile())
            {
                auto e = cab_.loadIR(slot, f);
                if (e.isNotEmpty())
                    irErrors << e << "\n";
            }
            else
                irErrors << "IR not found: " << ir.path << "\n";
        }
        else
            cab_.clearIR(slot);
        setIR(slot, ir);
    }
    auto applyKnobs = [this, s] {
        if (!model_)
            return;
        for (size_t i = 0; i < model_->info.knobs.size(); ++i)
        {
            // match by name first (exact recall even if the file changed order), then by index
            float v = model_->info.knobs[i].defaultValue;
            bool found = false;
            for (size_t j = 0; j < s.knobNames.size() && j < s.knobValues.size(); ++j)
                if (s.knobNames[j] == model_->info.knobs[i].name)
                    v = s.knobValues[j], found = true;
            if (!found && i < s.knobValues.size())
                v = s.knobValues[i];
            amp_.setKnob((int) i, v);
        }
    };
    juce::File mf(s.modelPath);
    if (!s.modelPath.empty() && mf.existsAsFile() && mf != modelFile_)
    {
        loadModel(mf, [applyKnobs, done, irErrors](juce::String err) {
            if (err.isEmpty())
                applyKnobs();
            if (done)
                done(irErrors + err);
        });
        return irErrors;
    }
    if (!s.modelPath.empty() && !mf.existsAsFile())
        irErrors << "Model not found: " << s.modelPath << " (" << s.modelName << ")\n";
    applyKnobs();
    if (done)
        done(irErrors);
    return irErrors;
}
} // namespace pnam
