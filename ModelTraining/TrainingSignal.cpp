#include "TrainingSignal.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

#include "WavIO.h"

namespace pnam
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Karplus-Strong plucked string with a pickup-ish tone control; returns `len` samples.
void pluck(std::vector<float>& out, size_t start, size_t len, double freq, float amp, float damping, float bright,
           double sr, std::mt19937& rng)
{
    const size_t period = std::max<size_t>(2, (size_t) std::lround(sr / freq));
    std::vector<float> line(period);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    // pick position comb: excitation = noise minus delayed noise
    for (auto& v : line)
        v = u(rng);
    float lp = 0.0f;
    for (size_t i = 0; i < period; ++i)
    {
        lp += bright * (line[i] - lp);
        line[i] = lp;
    }
    float prev = 0.0f;
    size_t idx = 0;
    for (size_t i = 0; i < len && start + i < out.size(); ++i)
    {
        const float cur = line[idx];
        const float next = damping * 0.5f * (cur + prev);
        prev = cur;
        line[idx] = next;
        idx = (idx + 1) % period;
        const float attackEnv = std::min(1.0f, (float) i / 24.0f);
        out[start + i] += amp * cur * attackEnv;
    }
}

float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
} // namespace

std::vector<float> makeTrainingSignal(const TrainingSignalOptions& o)
{
    const double sr = o.sampleRate;
    const size_t total = (size_t) (o.seconds * sr);
    std::vector<float> x(total, 0.0f);
    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    auto level = [&]() {
        // biased toward normal playing, with soft and hard extremes
        const float r = u01(rng);
        return dbToGain(o.softestDbfs + (o.peakDbfs - o.softestDbfs) * std::sqrt(r));
    };

    // Sections: 70% playing, 10% sweeps, 10% noise bursts, 10% held notes/quiet.
    const size_t playingEnd = (size_t) (0.70 * total), sweepEnd = (size_t) (0.80 * total), noiseEnd = (size_t) (0.90 * total);

    // 1) Playing: notes and chords from E2 (82.4 Hz) to ~E6 (1318 Hz)
    size_t t = (size_t) (0.05 * sr);
    const int chordShapes[4][6] = { { 0, 7, 12, 16, 19, 24 }, { 0, 7, 12, 15, 19, 24 }, { 0, 7, 12, -99, -99, -99 }, { 0, 5, 10, 15, 19, 24 } };
    while (t < playingEnd)
    {
        const float r = u01(rng);
        const int root = (int) (u01(rng) * 36.0f);           // semitones above E2
        const double f0 = 82.4069 * std::pow(2.0, root / 12.0);
        const bool palmMute = u01(rng) < 0.25f;
        const double durS = palmMute ? 0.06 + 0.12 * u01(rng) : 0.15 + 1.6 * u01(rng) * u01(rng);
        const size_t len = (size_t) (durS * sr);
        const float damping = palmMute ? 0.90f + 0.05f * u01(rng) : 0.994f + 0.0055f * u01(rng);
        const float bright = 0.25f + 0.7f * u01(rng);
        const float amp = level();
        if (r < 0.55f) // single note
            pluck(x, t, len + (size_t) (0.3 * sr), f0, amp, damping, bright, sr, rng);
        else           // chord, strummed
        {
            const auto& shape = chordShapes[(size_t) (u01(rng) * 3.99f)];
            const size_t strum = (size_t) ((0.004 + 0.02 * u01(rng)) * sr);
            int n = 0;
            for (int s = 0; s < 6; ++s)
                if (shape[s] > -50 && root + shape[s] < 48)
                {
                    pluck(x, t + (size_t) n * strum, len + (size_t) (0.4 * sr), f0 * std::pow(2.0, shape[s] / 12.0), amp * 0.45f,
                          damping, bright, sr, rng);
                    ++n;
                }
        }
        t += (size_t) (len * (0.6 + 0.6 * u01(rng)));
    }
    // 2) Log sine sweeps at several levels, 40 Hz .. 12 kHz
    {
        const float levels[] = { -30.0f, -18.0f, -9.0f, -3.0f };
        const size_t seg = (sweepEnd - playingEnd) / 4;
        for (int s = 0; s < 4; ++s)
        {
            const size_t a = playingEnd + (size_t) s * seg;
            const double T = (double) seg / sr * 0.9, f1 = 40.0, f2 = 12000.0, k = std::log(f2 / f1);
            for (size_t i = 0; i < (size_t) (T * sr); ++i)
            {
                const double tt = (double) i / sr;
                const double ph = 2 * kPi * f1 * T / k * (std::exp(tt / T * k) - 1.0);
                const float fade = (float) std::min({ 1.0, tt / 0.01, (T - tt) / 0.01 });
                x[a + i] += dbToGain(levels[s]) * fade * (float) std::sin(ph);
            }
        }
    }
    // 3) Band-limited noise bursts with envelopes at random levels
    {
        std::normal_distribution<float> n01(0.0f, 1.0f);
        size_t p = sweepEnd;
        float lp = 0.0f;
        while (p < noiseEnd)
        {
            const size_t len = (size_t) ((0.05 + 0.4 * u01(rng)) * sr);
            const float amp = level() * 0.35f, cut = 0.05f + 0.6f * u01(rng);
            for (size_t i = 0; i < len && p + i < noiseEnd; ++i)
            {
                lp += cut * (n01(rng) - lp);
                const float env = std::exp(-6.0f * (float) i / (float) len) * std::min(1.0f, (float) i / 48.0f);
                x[p + i] += amp * env * lp;
            }
            p += len + (size_t) (0.05 * sr * u01(rng));
        }
    }
    // 4) Held notes with slow swells, then near-silence (noise floor behaviour)
    {
        size_t p = noiseEnd;
        while (p + (size_t) (1.2 * sr) < total)
        {
            const double f = 82.4069 * std::pow(2.0, (int) (u01(rng) * 30.0f) / 12.0);
            pluck(x, p, (size_t) (1.2 * sr), f, level(), 0.9985f, 0.6f, sr, rng);
            p += (size_t) (1.0 * sr);
        }
    }
    // Soft-limit to the peak, so nothing clips digitally.
    const float peak = dbToGain(o.peakDbfs);
    for (auto& v : x)
        v = peak * std::tanh(v / peak);

    for (const auto& f : o.userDiFiles)
    {
        double fsr = 0;
        auto di = readWavMono(f, &fsr);
        if (std::abs(fsr - sr) > 0.5)
            throw std::runtime_error(f.filename().string() + ": DI file is " + std::to_string((int) fsr) + " Hz, training runs at "
                                     + std::to_string((int) sr) + " Hz");
        x.insert(x.end(), di.begin(), di.end());
    }
    return x;
}
} // namespace pnam
