#include "ModelProcessor.h"

#include <cmath>
#include <stdexcept>

namespace pnam
{
ModelProcessor::ModelProcessor()
{
    for (auto& k : knobTargets_)
        k.store(0.5f);
    for (auto& r : retired_)
        r.store(nullptr);
}

ModelProcessor::~ModelProcessor()
{
    collectGarbage();
    delete current_;
    delete fadingOut_;
    delete reference_;
    delete pendingModel_.exchange(nullptr);
    delete pendingRef_.exchange(nullptr);
}

void ModelProcessor::prepare(double hostRate, int maxBlock)
{
    hostRate_ = hostRate;
    maxBlock_ = std::max(1, maxBlock);
    gate_.prepare(hostRate);
    lastGateThresh_ = 1e9f;
    inGainS_.prepare(hostRate, 20.0f);
    outGainS_.prepare(hostRate, 20.0f);
    abMix_.prepare(hostRate, 15.0f);
    inGainS_.snap(inGain_.load());
    outGainS_.snap(outGain_.load());
    dry_.assign((size_t) maxBlock_, 0.0f);
    bufA_.assign((size_t) maxBlock_, 0.0f);
    bufB_.assign((size_t) maxBlock_, 0.0f);
    fadeLen_ = (int) (0.03 * hostRate); // 30 ms equal-gain crossfade on model change
    // re-prepare existing slots for the new rate / block size (audio is stopped)
    auto reprep = [&](ModelSlot* s) {
        if (!s)
            return;
        s->rs.prepare(hostRate_, s->modelRate, maxBlock_);
        s->innerMax = s->rs.active() ? (int) std::ceil(maxBlock_ * s->modelRate / hostRate_) + 8 : maxBlock_;
        s->work.assign((size_t) s->innerMax, 0.0f);
        s->dsp->ResetAndPrewarm(s->modelRate, kSubBlock);
        for (auto& k : s->knobSmooth)
            k.prepare(s->modelRate / kSubBlock, 25.0f);
    };
    reprep(current_);
    reprep(reference_);
    if (fadingOut_)
    {
        retire(fadingOut_);
        fadingOut_ = nullptr;
    }
    latency_.store(current_ ? current_->rs.latencySamples() : 0);
}

std::unique_ptr<ModelSlot> ModelProcessor::makeSlot(std::unique_ptr<nam::DSP> dsp, const NamFileInfo& info, ParametricWaveNet* p,
                                                    const std::vector<float>* initialKnobs) const
{
    if (!dsp)
        throw std::runtime_error("no model");
    auto s = std::make_unique<ModelSlot>();
    s->dsp = std::move(dsp);
    s->parametric = p;
    s->info = info;
    s->modelRate = s->dsp->GetExpectedSampleRate() > 0 ? s->dsp->GetExpectedSampleRate() : 48000.0;
    s->rs.prepare(hostRate_, s->modelRate, maxBlock_);
    s->innerMax = s->rs.active() ? (int) std::ceil(maxBlock_ * s->modelRate / hostRate_) + 8 : maxBlock_;
    s->work.assign((size_t) s->innerMax, 0.0f);
    for (auto& k : s->knobSmooth)
        k.prepare(s->modelRate / kSubBlock, 25.0f);
    if (p)
        for (int i = 0; i < p->knobCount(); ++i)
            p->setKnob(i, p->knob(i).def);
    s->normGain = measureNormGain(*s);
    // start at the current control positions, so the prewarmed history matches what will play
    if (p)
        for (int i = 0; i < p->knobCount(); ++i)
            p->setKnob(i, initialKnobs && i < (int) initialKnobs->size() ? (*initialKnobs)[(size_t) i]
                                                                          : knobTargets_[(size_t) i].load(std::memory_order_relaxed));
    // models process in sub-blocks so control changes land every kSubBlock samples
    s->dsp->ResetAndPrewarm(s->modelRate, kSubBlock);
    // flush the whole receptive field with silence at these settings (Reset does not clear every history buffer)
    {
        const int flush = std::max(info.receptiveField, (int) (0.1 * s->modelRate)) + kSubBlock;
        std::vector<float> z((size_t) kSubBlock, 0.0f), o((size_t) kSubBlock);
        for (int p0 = 0; p0 < flush; p0 += kSubBlock)
        {
            NAM_SAMPLE* ip = z.data();
            NAM_SAMPLE* op = o.data();
            s->dsp->process(&ip, &op, kSubBlock);
        }
    }
    return s;
}

float ModelProcessor::measureNormGain(ModelSlot& s)
{
    // RMS of the model at its default setting for a guitar-level test tone burst; normalise to -18 dBFS RMS.
    s.dsp->ResetAndPrewarm(s.modelRate, 512);
    const int n = (int) (0.5 * s.modelRate);
    std::vector<float> x((size_t) n), y((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / s.modelRate;
        x[(size_t) i] = (float) (0.1 * std::exp(-3.0 * std::fmod(t, 0.25)) * (std::sin(2 * M_PI * 110 * t) + 0.5 * std::sin(2 * M_PI * 330 * t)));
    }
    for (int p = 0; p < n; p += 512)
    {
        const int m = std::min(512, n - p);
        NAM_SAMPLE* ip = x.data() + p;
        NAM_SAMPLE* op = y.data() + p;
        s.dsp->process(&ip, &op, m);
    }
    double e = 0;
    for (int i = n / 4; i < n; ++i)
        e += (double) y[(size_t) i] * y[(size_t) i];
    const double rms = std::sqrt(e / (n - n / 4));
    if (!(rms > 1e-7) || !std::isfinite(rms))
        return 1.0f;
    return (float) std::clamp(std::pow(10.0, -18.0 / 20.0) / rms, 0.01, 100.0);
}

void ModelProcessor::setModel(std::unique_ptr<ModelSlot> slot)
{
    delete pendingModel_.exchange(slot.release());
    hasModel_.store(true);
}

void ModelProcessor::setReference(std::unique_ptr<ModelSlot> slot)
{
    if (!slot)
        clearRef_.store(true);
    delete pendingRef_.exchange(slot.release());
}

void ModelProcessor::setKnob(int i, float v)
{
    if (i >= 0 && i < (int) knobTargets_.size())
        knobTargets_[(size_t) i].store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed);
}

void ModelProcessor::setGate(bool on, float th)
{
    gateOn_.store(on);
    gateThresh_.store(th);
}

void ModelProcessor::retire(ModelSlot* s)
{
    for (auto& r : retired_)
    {
        ModelSlot* expected = nullptr;
        if (r.compare_exchange_strong(expected, s))
            return;
    }
    // Queue full (UI thread stalled): keep it alive rather than free on the audio thread.
    // This leaks one model in a pathological case instead of risking a glitch.
}

void ModelProcessor::collectGarbage()
{
    for (auto& r : retired_)
        delete r.exchange(nullptr);
}

void ModelProcessor::runSlot(ModelSlot& s, const float* in, float* out, int n, bool smoothKnobs)
{
    auto inner = [&](float* buf, int m) {
        for (int p = 0; p < m; p += kSubBlock)
        {
            const int k = std::min(kSubBlock, m - p);
            if (s.parametric && smoothKnobs)
            {
                for (int i = 0; i < s.parametric->knobCount(); ++i)
                {
                    const float target = knobTargets_[(size_t) i].load(std::memory_order_relaxed);
                    if (!s.knobsInitialised)
                        s.knobSmooth[(size_t) i].snap(target);
                    float v = s.knobSmooth[(size_t) i].next(target);
                    if (std::fabs(v - target) < 1e-4f)
                        v = target;
                    if (s.parametric->knob(i).labels.size() >= 2)
                        v = target; // switches step; the crossfade-free jump is what the real switch does
                    if (v != s.parametric->knobValue(i))
                        s.parametric->setKnob(i, v);
                }
                s.knobsInitialised = true;
            }
            NAM_SAMPLE* ip = buf + p;
            NAM_SAMPLE* op = s.work.data();
            s.dsp->process(&ip, &op, k);
            for (int i = 0; i < k; ++i)
                buf[p + i] = s.work[(size_t) i];
        }
    };
    s.rs.process(in, out, n, inner);
    const float g = normalize_.load(std::memory_order_relaxed) ? s.normGain : 1.0f;
    for (int i = 0; i < n; ++i)
        out[i] *= g;
}

void ModelProcessor::processBlock(const float* in, float* out, int n)
{
    if (n > maxBlock_)
    {
        for (int p = 0; p < n; p += maxBlock_)
            processBlock(in + p, out + p, std::min(maxBlock_, n - p));
        return;
    }
    // pick up new models
    if (ModelSlot* incoming = pendingModel_.exchange(nullptr))
    {
        if (fadingOut_)
            retire(fadingOut_);
        fadingOut_ = current_;
        current_ = incoming;
        fadePos_ = 0;
        latency_.store(current_->rs.latencySamples());
    }
    if (ModelSlot* r = pendingRef_.exchange(nullptr))
    {
        if (reference_)
            retire(reference_);
        reference_ = r;
    }
    else if (clearRef_.exchange(false) && reference_)
    {
        retire(reference_);
        reference_ = nullptr;
    }

    inputMeter.measure(in, n);
    const float ig = inGain_.load(std::memory_order_relaxed), og = outGain_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        dry_[(size_t) i] = in[i] * inGainS_.next(ig);

    if (!current_)
    {
        for (int i = 0; i < n; ++i)
            out[i] = 0.0f;
        outputMeter.measure(out, n);
        return;
    }

    runSlot(*current_, dry_.data(), out, n, true);
    if (fadingOut_)
    {
        runSlot(*fadingOut_, dry_.data(), bufA_.data(), n, false);
        for (int i = 0; i < n; ++i)
        {
            const float w = std::min(1.0f, (float) (fadePos_ + i) / (float) fadeLen_);
            out[i] = w * out[i] + (1 - w) * bufA_[(size_t) i];
        }
        fadePos_ += n;
        if (fadePos_ >= fadeLen_)
        {
            retire(fadingOut_);
            fadingOut_ = nullptr;
        }
    }
    // A/B: the reference capture runs continuously so switching is instant and click-free
    if (reference_)
    {
        runSlot(*reference_, dry_.data(), bufB_.data(), n, false);
        const float target = abReference_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float w = abMix_.next(target);
            out[i] = (1 - w) * out[i] + w * bufB_[(size_t) i];
        }
    }

    const float th = gateThresh_.load(std::memory_order_relaxed);
    if (th != lastGateThresh_)
    {
        gate_.setParams(th);
        lastGateThresh_ = th;
    }
    if (gateOn_.load(std::memory_order_relaxed))
        gate_.process(dry_.data(), out, n); // NOTE: detector is the dry input; the latency of a resampled model is tiny vs gate hold
    for (int i = 0; i < n; ++i)
    {
        out[i] *= outGainS_.next(og);
        if (!std::isfinite(out[i]))
            out[i] = 0.0f;
    }
    outputMeter.measure(out, n);
}
} // namespace pnam
