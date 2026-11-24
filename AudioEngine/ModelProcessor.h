#pragma once
// The real-time amp stage: runs the loaded Parametric NAM model (or, for A/B, a source capture)
// at its own sample rate, with smoothed control changes, click-free model switching and a noise
// gate. JUCE-free so the test-suite can drive it directly.
//
// Threading: load*/set* are called from the message thread; processBlock() from the audio
// thread. Models are built and prepared off the audio thread and handed over through a
// single-slot atomic; the audio thread never allocates, locks or frees.
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <NAM/dsp.h>

#include "NamFileInfo.h"
#include "ParametricWaveNet.h"
#include "dsp/Dynamics.h"
#include "dsp/Resampler.h"

namespace pnam
{
struct ModelSlot
{
    std::unique_ptr<nam::DSP> dsp;
    ParametricWaveNet* parametric = nullptr; // == dsp.get() for parametric models
    NamFileInfo info;
    double modelRate = 48000;
    float normGain = 1.0f;                    // loudness normalisation at the default setting
    ResampledProcessor rs;
    std::array<Smoothed, ParametricWaveNet::kMaxKnobs> knobSmooth;
    std::vector<float> work;
    int innerMax = 0;
    bool knobsInitialised = false;
};

class ModelProcessor
{
public:
    static constexpr int kSubBlock = 32; // control changes are applied every 32 samples

    ModelProcessor();
    ~ModelProcessor();

    // message thread, audio stopped
    void prepare(double hostRate, int maxBlock);
    double hostRate() const { return hostRate_; }
    int maxBlock() const { return maxBlock_; }

    // Builds a slot ready to play (any thread except audio). Throws on failure.
    // initialKnobs: control positions to start at (default: the current control targets).
    std::unique_ptr<ModelSlot> makeSlot(std::unique_ptr<nam::DSP> dsp, const NamFileInfo& info, ParametricWaveNet* parametric,
                                        const std::vector<float>* initialKnobs = nullptr) const;
    // Hands a prepared slot to the audio thread (crossfades from the current one).
    void setModel(std::unique_ptr<ModelSlot> slot);
    void setReference(std::unique_ptr<ModelSlot> slot); // A/B source capture (plain NAM), or nullptr
    void setABListenToReference(bool b) { abReference_.store(b); }
    bool abListenToReference() const { return abReference_.load(); }

    void setKnob(int i, float v01);
    float knob(int i) const { return knobTargets_[(size_t) i].load(std::memory_order_relaxed); }
    void setInputGainDb(float db) { inGain_.store(std::pow(10.0f, db / 20.0f)); }
    void setOutputGainDb(float db) { outGain_.store(std::pow(10.0f, db / 20.0f)); }
    void setGate(bool on, float thresholdDb);
    void setNormalize(bool b) { normalize_.store(b); }

    // audio thread: mono in -> mono out (amp stage incl. gate and gains, no cabinet)
    void processBlock(const float* in, float* out, int n);

    // message thread housekeeping: frees retired slots
    void collectGarbage();

    // info for the UI
    int latencySamples() const { return latency_.load(); }
    bool hasModel() const { return hasModel_.load(); }
    LevelMeter inputMeter, outputMeter;
    float gateGain() const { return gate_.currentGain(); }

    // offline helper for tests and analysis
    static float measureNormGain(ModelSlot& s);

private:
    void runSlot(ModelSlot& s, const float* in, float* out, int n, bool smoothKnobs);
    void retire(ModelSlot* s);

    double hostRate_ = 48000;
    int maxBlock_ = 512;
    std::atomic<ModelSlot*> pendingModel_ { nullptr }, pendingRef_ { nullptr };
    std::atomic<bool> clearRef_ { false };
    ModelSlot* current_ = nullptr;   // audio thread owned
    ModelSlot* fadingOut_ = nullptr; // audio thread owned
    ModelSlot* reference_ = nullptr; // audio thread owned
    int fadePos_ = 0, fadeLen_ = 1;
    std::array<std::atomic<ModelSlot*>, 16> retired_ {};
    std::array<std::atomic<float>, ParametricWaveNet::kMaxKnobs> knobTargets_ {};
    std::atomic<float> inGain_ { 1.0f }, outGain_ { 1.0f };
    std::atomic<bool> gateOn_ { true }, normalize_ { true }, abReference_ { false }, hasModel_ { false };
    std::atomic<float> gateThresh_ { -70.0f };
    std::atomic<int> latency_ { 0 };
    NoiseGate gate_;
    Smoothed inGainS_, outGainS_, abMix_;
    float lastGateThresh_ = 1e9f;
    std::vector<float> dry_, bufA_, bufB_;
};
} // namespace pnam
