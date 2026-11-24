#include "DualIRCabinet.h"

namespace pnam
{
DualIRCabinet::DualIRCabinet()
{
    formats_.registerBasicFormats();
    for (size_t i = 0; i < 2; ++i)
    {
        conv_[i] = std::make_unique<juce::dsp::Convolution>(juce::dsp::Convolution::NonUniform { 256 });
        enabled_[i].store(i == 0);
        invert_[i].store(false);
        hasIR_[i].store(false);
        level_[i].store(1.0f);
        pan_[i].store(0.0f);
    }
}

void DualIRCabinet::prepare(double sr, int maxBlock)
{
    maxBlock_ = maxBlock;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
    for (size_t i = 0; i < 2; ++i)
    {
        conv_[i]->prepare(spec);
        gl_[i].reset(sr, 0.03);
        gr_[i].reset(sr, 0.03);
    }
    tmp_.setSize(2, maxBlock, false, true, true);
}

void DualIRCabinet::reset()
{
    for (auto& c : conv_)
        c->reset();
}

juce::String DualIRCabinet::loadIR(int slot, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> r(formats_.createReaderFor(file));
    if (!r)
        return "Cannot read " + file.getFileName() + " as an audio file.";
    if (r->lengthInSamples <= 0 || r->lengthInSamples > (juce::int64) r->sampleRate * 10)
        return file.getFileName() + ": impulse response must be between 1 sample and 10 seconds long.";
    juce::AudioBuffer<float> buf((int) r->numChannels, (int) r->lengthInSamples);
    r->read(&buf, 0, (int) r->lengthInSamples, 0, true, true);
    // stereo IRs are summed to mono: the slot's pan places it in the stereo field
    juce::AudioBuffer<float> mono(1, buf.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        mono.addFrom(0, 0, buf, ch, 0, buf.getNumSamples(), 1.0f / (float) buf.getNumChannels());
    IRInfo in;
    in.name = file.getFileNameWithoutExtension();
    in.path = file.getFullPathName();
    in.sampleRate = r->sampleRate;
    in.samples = (int) r->lengthInSamples;
    in.channels = (int) r->numChannels;
    conv_[(size_t) slot]->loadImpulseResponse(std::move(mono), r->sampleRate, juce::dsp::Convolution::Stereo::no,
                                              juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
    info_[(size_t) slot] = in;
    hasIR_[(size_t) slot].store(true);
    return {};
}

void DualIRCabinet::clearIR(int slot)
{
    hasIR_[(size_t) slot].store(false);
    info_[(size_t) slot] = {};
}

int DualIRCabinet::latencySamples() const
{
    return juce::jmax(conv_[0]->getLatency(), conv_[1]->getLatency());
}

void DualIRCabinet::process(const float* mono, float* left, float* right, int n)
{
    const bool any = !bypass_.load() && ((enabled_[0].load() && hasIR_[0].load()) || (enabled_[1].load() && hasIR_[1].load()));
    if (!any)
    {
        juce::FloatVectorOperations::copy(left, mono, n);
        juce::FloatVectorOperations::copy(right, mono, n);
        return;
    }
    juce::FloatVectorOperations::clear(left, n);
    juce::FloatVectorOperations::clear(right, n);
    for (size_t i = 0; i < 2; ++i)
    {
        const bool on = enabled_[i].load() && hasIR_[i].load();
        // equal-power pan; smoothing keeps level and pan moves click-free
        const float lvl = on ? level_[i].load() * (invert_[i].load() ? -1.0f : 1.0f) : 0.0f;
        const float p = (pan_[i].load() + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        gl_[i].setTargetValue(lvl * std::cos(p) * juce::MathConstants<float>::sqrt2);
        gr_[i].setTargetValue(lvl * std::sin(p) * juce::MathConstants<float>::sqrt2);
        if (!on && !gl_[i].isSmoothing() && !gr_[i].isSmoothing())
            continue;
        float* t = tmp_.getWritePointer(0);
        juce::FloatVectorOperations::copy(t, mono, n);
        juce::dsp::AudioBlock<float> block(&t, 1, (size_t) n);
        conv_[i]->process(juce::dsp::ProcessContextReplacing<float>(block));
        for (int s = 0; s < n; ++s)
        {
            left[s] += t[s] * gl_[i].getNextValue();
            right[s] += t[s] * gr_[i].getNextValue();
        }
    }
}
} // namespace pnam
