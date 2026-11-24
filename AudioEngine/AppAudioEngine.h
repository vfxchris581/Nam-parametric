#pragma once
// The application's audio engine: audio device I/O around the signal chain
//   Input -> input gain -> Parametric NAM (own sample rate) -> noise gate -> Cabinet (dual IR) -> output
// and the thread-safe entry points the UI uses (load model, controls, cabinet, presets).
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "DualIRCabinet.h"
#include "ModelProcessor.h"
#include "ParametricModel.h"
#include "Presets.h"

namespace pnam
{
class AppAudioEngine : public juce::AudioIODeviceCallback
{
public:
    AppAudioEngine();
    ~AppAudioEngine() override;

    juce::AudioDeviceManager& deviceManager() { return devices_; }
    void initialiseAudio(const juce::XmlElement* savedState);

    // ---- model (message thread; loading happens on a background thread)
    using LoadCallback = std::function<void(juce::String error)>;
    void loadModel(const juce::File& file, LoadCallback done);
    void loadModelBlocking(const juce::File& file); // throws; used by tests/screenshots
    bool hasModel() const { return model_.has_value(); }
    const NamFileInfo* modelInfo() const { return model_ ? &model_->info : nullptr; }
    juce::File modelFile() const { return modelFile_; }
    void setKnob(int i, float v01) { amp_.setKnob(i, v01); }
    float knob(int i) const { return amp_.knob(i); }

    // A/B against a source capture (plain NAM) - Capture Studio comparisons
    juce::String loadReference(const juce::File& file);
    void clearReference();
    void setListenToReference(bool b) { amp_.setABListenToReference(b); }
    bool hasReference() const { return referenceFile_.existsAsFile(); }
    juce::File referenceFile() const { return referenceFile_; }

    // ---- chain
    void setInputGainDb(float db) { state_.inputGainDb = db; amp_.setInputGainDb(db); }
    void setOutputGainDb(float db) { state_.outputGainDb = db; amp_.setOutputGainDb(db); }
    void setGate(bool on, float th) { state_.gateOn = on; state_.gateThresholdDb = th; amp_.setGate(on, th); }
    void setNormalize(bool b) { state_.normalizeLoudness = b; amp_.setNormalize(b); }
    void setStereo(bool b) { state_.stereo = b; stereo_.store(b); }
    void setInputChannel(int ch) { inputChannel_.store(ch); }
    int inputChannel() const { return inputChannel_.load(); }
    DualIRCabinet& cabinet() { return cab_; }
    juce::String loadIR(int slot, const juce::File& f);
    void setIR(int slot, const IRSlotSettings& s);
    void setCabEnabled(bool b) { state_.cabEnabled = b; cab_.setBypassed(!b); }

    // ---- presets
    PresetState captureState() const;            // current settings as a preset (name left empty)
    juce::String applyState(const PresetState& s, std::function<void(juce::String)> done = {});
    const PresetState& state() const { return state_; }

    // ---- meters & info
    ModelProcessor& amp() { return amp_; }
    float takeOutputPeak() { return outMeterPeak_.exchange(0.0f); }
    int totalLatencySamples() const;
    double currentSampleRate() const { return sampleRate_; }
    int currentBlockSize() const { return blockSize_; }

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override;

private:
    juce::AudioDeviceManager devices_;
    ModelProcessor amp_;
    DualIRCabinet cab_;
    juce::ThreadPool loader_ { 1 };
    std::optional<NamFileInfo> modelInfoStore_;
    struct ModelMeta { NamFileInfo info; nlohmann::json json; };
    std::optional<ModelMeta> model_;
    juce::File modelFile_, referenceFile_;
    PresetState state_;
    std::atomic<bool> stereo_ { true };
    std::atomic<int> inputChannel_ { 0 };
    std::atomic<float> outMeterPeak_ { 0.0f };
    std::vector<float> monoIn_, ampOut_, left_, right_;
    double sampleRate_ = 48000;
    int blockSize_ = 256;
    std::atomic<int> generation_ { 0 };
};
} // namespace pnam
