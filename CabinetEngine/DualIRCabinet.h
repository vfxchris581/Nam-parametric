#pragma once
// Cabinet section: two impulse-response slots (A and B), each with level, pan, polarity and
// enable, blended into a stereo pair. Zero-latency non-uniform partitioned convolution.
// IRs are resampled by JUCE to the running sample rate. IR loading never blocks the audio thread.
#include <array>
#include <atomic>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace pnam
{
class DualIRCabinet
{
public:
    struct IRInfo
    {
        juce::String name, path;
        double sampleRate = 0;
        int samples = 0;
        int channels = 0;
        bool loaded() const { return samples > 0; }
    };

    DualIRCabinet();
    void prepare(double sampleRate, int maxBlock);
    void reset();

    // Message thread. Returns an error message, or empty on success.
    juce::String loadIR(int slot, const juce::File& file);
    void clearIR(int slot);
    IRInfo info(int slot) const { return info_[(size_t) slot]; }

    void setEnabled(int slot, bool b) { enabled_[(size_t) slot].store(b); }
    void setLevelDb(int slot, float db) { level_[(size_t) slot].store(juce::Decibels::decibelsToGain(db)); }
    void setPan(int slot, float pan) { pan_[(size_t) slot].store(juce::jlimit(-1.0f, 1.0f, pan)); }
    void setInvert(int slot, bool b) { invert_[(size_t) slot].store(b); }
    void setBypassed(bool b) { bypass_.store(b); }
    bool enabled(int slot) const { return enabled_[(size_t) slot].load(); }
    bool bypassed() const { return bypass_.load(); }

    // Audio thread: mono amp signal in, stereo out.
    void process(const float* mono, float* left, float* right, int n);
    int latencySamples() const;

private:
    std::array<std::unique_ptr<juce::dsp::Convolution>, 2> conv_;
    std::array<IRInfo, 2> info_;
    std::array<std::atomic<bool>, 2> enabled_ {}, invert_ {}, hasIR_ {};
    std::array<std::atomic<float>, 2> level_ {}, pan_ {};
    std::atomic<bool> bypass_ { false };
    std::array<juce::LinearSmoothedValue<float>, 2> gl_, gr_;
    juce::AudioBuffer<float> tmp_;
    juce::AudioFormatManager formats_;
    int maxBlock_ = 512;
};
} // namespace pnam
