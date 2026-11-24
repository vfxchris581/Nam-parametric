#include "Resampler.h"

#include <algorithm>
#include <cmath>

namespace pnam
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
double besselI0(double x)
{
    double s = 1, t = 1;
    for (int k = 1; k < 40; ++k)
    {
        t *= (x / (2 * k)) * (x / (2 * k));
        s += t;
        if (t < 1e-12 * s)
            break;
    }
    return s;
}
} // namespace

void StreamResampler::prepare(double inRate, double outRate, int maxIn, int zc)
{
    inRate_ = inRate;
    outRate_ = outRate;
    step_ = inRate / outRate;
    scale_ = std::min(1.0, outRate / inRate);  // < 1 when decimating
    const double cutoff = 0.5 * scale_ * 0.94;   // in cycles per input sample (passband to ~0.47 fs_low)
    halfLen_ = (int) std::ceil(zc / scale_);
    const double beta = 9.0;
    const int n = halfLen_ * tableRes_ + 2;
    table_.assign((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double tau = (double) i / tableRes_;
        const double x = 2 * cutoff * tau;
        const double sinc = tau == 0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double r = tau / halfLen_;
        const double win = r >= 1 ? 0.0 : besselI0(beta * std::sqrt(1 - r * r)) / besselI0(beta);
        table_[(size_t) i] = (float) (2 * cutoff * sinc * win);
    }
    hist_.assign((size_t) (maxIn + 4 * halfLen_ + 8), 0.0f);
    reset();
}

void StreamResampler::reset()
{
    std::fill(hist_.begin(), hist_.end(), 0.0f);
    // Pre-fill one half-kernel of zeros so output starts immediately with a fixed delay.
    histFill_ = 2 * halfLen_;
    t_ = halfLen_;
}

float StreamResampler::kernel(double tau) const
{
    tau = std::fabs(tau);
    const double p = tau * tableRes_;
    const int i = (int) p;
    if (i + 1 >= (int) table_.size())
        return 0.0f;
    const float f = (float) (p - i);
    return table_[(size_t) i] + f * (table_[(size_t) i + 1] - table_[(size_t) i]);
}

int StreamResampler::process(const float* in, int n, float* out, int maxOut)
{
    // append input
    std::copy(in, in + n, hist_.begin() + histFill_);
    histFill_ += n;
    int written = 0;
    while (written < maxOut && t_ + halfLen_ < histFill_ - 1)
    {
        const int c = (int) std::floor(t_);
        const double frac = t_ - c;
        double acc = 0;
        for (int j = -halfLen_ + 1; j <= halfLen_; ++j)
            acc += hist_[(size_t) (c + j)] * kernel((double) j - frac);
        out[written++] = (float) acc;
        t_ += step_;
    }
    // discard history no longer needed
    const int keepFrom = std::max(0, (int) std::floor(t_) - halfLen_);
    if (keepFrom > 0)
    {
        std::copy(hist_.begin() + keepFrom, hist_.begin() + histFill_, hist_.begin());
        histFill_ -= keepFrom;
        t_ -= keepFrom;
    }
    return written;
}

void ResampledProcessor::prepare(double hostRate, double innerRate, int maxHostBlock)
{
    active_ = std::fabs(hostRate - innerRate) > 0.5;
    if (!active_)
    {
        latency_ = 0;
        return;
    }
    up_.prepare(hostRate, innerRate, maxHostBlock);
    const int maxInner = up_.maxOutputFor(maxHostBlock);
    down_.prepare(innerRate, hostRate, maxInner);
    inner_.assign((size_t) maxInner, 0.0f);
    back_.assign((size_t) down_.maxOutputFor(maxInner), 0.0f);
    fifo_.assign((size_t) (4 * maxHostBlock + 64), 0.0f);
    prime_ = 3 + (int) std::ceil(hostRate / innerRate); // output count per block jitters by up to one inner sample
    latency_ = (int) std::lround(up_.latencyInputSamples() + down_.latencyInputSamples() * hostRate / innerRate) + prime_;
    reset();
}

void ResampledProcessor::reset()
{
    if (!active_)
        return;
    up_.reset();
    down_.reset();
    head_ = 0;
    count_ = 0;
    underruns_ = 0;
    for (int i = 0; i < prime_; ++i)
        push(0.0f);
}
} // namespace pnam
