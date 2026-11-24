#pragma once
// Small real-time helpers: a noise gate (detects on the guitar input, attenuates the amp
// output), parameter smoothing and peak/RMS metering. Allocation-free.
#include <algorithm>
#include <atomic>
#include <cmath>

namespace pnam
{
class NoiseGate
{
public:
    void prepare(double sr)
    {
        sr_ = sr;
        env_ = 0;
        gain_ = 1;
        holdLeft_ = 0;
        open_ = true;
    }
    void setParams(float thresholdDb, float releaseMs = 120.0f, float hysteresisDb = 6.0f)
    {
        openLin_ = std::pow(10.0f, thresholdDb / 20.0f);
        closeLin_ = std::pow(10.0f, (thresholdDb - hysteresisDb) / 20.0f);
        relCoef_ = std::exp(-1.0f / (0.001f * releaseMs * (float) sr_));
    }
    // detector: the dry input; target: the signal to attenuate (in place)
    void process(const float* detector, float* target, int n)
    {
        const float envCoef = std::exp(-1.0f / (0.005f * (float) sr_)); // 5 ms detector
        const float atkCoef = std::exp(-1.0f / (0.0005f * (float) sr_));
        const int hold = (int) (0.05 * sr_);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::fabs(detector[i]);
            env_ = a > env_ ? a : envCoef * env_ + (1 - envCoef) * a;
            if (env_ > openLin_)
                open_ = true, holdLeft_ = hold;
            else if (env_ < closeLin_)
            {
                if (holdLeft_ > 0)
                    --holdLeft_;
                else
                    open_ = false;
            }
            const float targetGain = open_ ? 1.0f : 0.0f;
            const float c = targetGain > gain_ ? atkCoef : relCoef_;
            gain_ = c * gain_ + (1 - c) * targetGain;
            target[i] *= gain_;
        }
        reduction_.store(gain_, std::memory_order_relaxed);
    }
    float currentGain() const { return reduction_.load(std::memory_order_relaxed); }

private:
    double sr_ = 48000;
    float openLin_ = 0.001f, closeLin_ = 0.0005f, relCoef_ = 0.999f;
    float env_ = 0, gain_ = 1;
    int holdLeft_ = 0;
    bool open_ = true;
    std::atomic<float> reduction_ { 1.0f };
};

// One-pole smoothing toward a target, per sample or per sub-block.
class Smoothed
{
public:
    void prepare(double rate, float timeMs) { coef_ = std::exp(-1.0f / std::max(1e-6f, 0.001f * timeMs * (float) rate)); }
    void snap(float v) { cur_ = v; }
    float next(float target) { cur_ = coef_ * cur_ + (1 - coef_) * target; return cur_; }
    float value() const { return cur_; }

private:
    float coef_ = 0.99f, cur_ = 0;
};

struct LevelMeter
{
    std::atomic<float> peak { 0 }, rms { 0 };
    void measure(const float* x, int n)
    {
        float p = 0;
        double s = 0;
        for (int i = 0; i < n; ++i)
        {
            p = std::max(p, std::fabs(x[i]));
            s += (double) x[i] * x[i];
        }
        // the UI reads and decays; keep the max since the last read
        float old = peak.load(std::memory_order_relaxed);
        while (p > old && !peak.compare_exchange_weak(old, p, std::memory_order_relaxed)) {}
        rms.store(n ? (float) std::sqrt(s / n) : 0.0f, std::memory_order_relaxed);
    }
    float takePeak() { return peak.exchange(0.0f, std::memory_order_relaxed); }
};
} // namespace pnam
