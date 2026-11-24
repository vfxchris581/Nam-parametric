#pragma once
// Band-limited streaming sample-rate conversion (windowed-sinc, Kaiser window, arbitrary ratio).
// Used to run a NAM model at the rate it was trained at whatever rate the audio interface runs.
// When decimating, the kernel is stretched so the cutoff follows the lower rate (anti-aliasing).
// No allocation after prepare().
#include <vector>

namespace pnam
{
class StreamResampler
{
public:
    // maxInputBlock: the largest number of input samples that process() will ever be given.
    void prepare(double inRate, double outRate, int maxInputBlock, int zeroCrossings = 24);
    void reset();
    // Consumes all n inputs; writes up to maxOut outputs, returns how many were written.
    int process(const float* in, int n, float* out, int maxOut);
    // Group delay in INPUT samples.
    double latencyInputSamples() const { return halfLen_; }
    double ratio() const { return outRate_ / inRate_; }
    int maxOutputFor(int nIn) const { return (int) (nIn * ratio()) + 4; }

private:
    double inRate_ = 48000, outRate_ = 48000, step_ = 1, scale_ = 1;
    int halfLen_ = 24;             // kernel half-length in input samples
    int tableRes_ = 512;           // table points per input sample
    std::vector<float> table_;     // h(tau) for tau = 0 .. halfLen_, step 1/tableRes_ (symmetric)
    std::vector<float> hist_;      // linear history: [0 .. histFill_)
    int histFill_ = 0;
    double t_ = 0;                 // position of the next output, in history samples
    float kernel(double tau) const;
};

// Runs a mono process function at another sample rate, returning exactly n samples per call
// (fixed latency, reported by latencySamples()).
class ResampledProcessor
{
public:
    void prepare(double hostRate, double innerRate, int maxHostBlock);
    bool active() const { return active_; }
    template <typename Fn> // Fn(float* buf, int n) processes in place at the inner rate
    void process(const float* in, float* out, int n, Fn&& fn)
    {
        if (!active_)
        {
            for (int i = 0; i < n; ++i)
                out[i] = in[i];
            fn(out, n);
            return;
        }
        const int m = up_.process(in, n, inner_.data(), (int) inner_.size());
        fn(inner_.data(), m);
        const int k = down_.process(inner_.data(), m, back_.data(), (int) back_.size());
        for (int i = 0; i < k; ++i)
            push(back_[(size_t) i]);
        for (int i = 0; i < n; ++i)
            out[i] = pop();
    }
    int latencySamples() const { return latency_; }
    int underruns() const { return underruns_; }
    void reset();

private:
    void push(float v)
    {
        if (count_ < (int) fifo_.size())
        {
            fifo_[(size_t) ((head_ + count_) % (int) fifo_.size())] = v;
            ++count_;
        }
    }
    float pop()
    {
        if (count_ == 0)
        {
            ++underruns_;
            return 0.0f;
        }
        const float v = fifo_[(size_t) head_];
        head_ = (head_ + 1) % (int) fifo_.size();
        --count_;
        return v;
    }
    bool active_ = false;
    StreamResampler up_, down_;
    std::vector<float> inner_, back_, fifo_;
    int head_ = 0, count_ = 0, latency_ = 0, prime_ = 0, underruns_ = 0;
};
} // namespace pnam
