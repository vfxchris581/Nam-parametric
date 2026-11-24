#pragma once
// Objective measurements used by training validation, the A/B report and the test-suite.
#include <functional>
#include <string>
#include <vector>

namespace pnam
{
// Error-to-signal ratio: sum (y - yhat)^2 / sum y^2. 0 is a perfect match; NAM's usual
// single-capture models reach roughly 0.005-0.02 on their validation audio.
double esr(const std::vector<float>& reference, const std::vector<float>& estimate, size_t skip = 0);

// One label for an ESR, so the UI never words a weak model as accurate.
std::string esrVerdict(double esr);

// A system to measure: takes a mono block, returns the processed block (offline, stateful between calls is fine).
using RenderFn = std::function<std::vector<float>(const std::vector<float>&)>;

struct HarmonicProfile
{
    double fundamentalDb = 0;               // level of the fundamental at the output, dBFS
    std::vector<double> harmonicsDb;         // H2..H(n) relative to the fundamental, dB
    double thdPercent = 0;
};
// Plays a sine at `freq` Hz and `levelDbfs`, measures harmonics 2..maxHarmonic on the settled output.
HarmonicProfile measureHarmonics(const RenderFn& render, double sampleRate, double freq, double levelDbfs, int maxHarmonic = 8);

struct ResponsePoint { double freq, gainDb; };
// Small-signal-ish magnitude response from stepped sines (Goertzel) at `levelDbfs`.
std::vector<ResponsePoint> measureResponse(const RenderFn& render, double sampleRate, double levelDbfs,
                                           const std::vector<double>& freqs = {});

// Level of the output vs input level (dynamics / compression curve), 1 kHz sine.
std::vector<std::pair<double, double>> measureGainCurve(const RenderFn& render, double sampleRate,
                                                        const std::vector<double>& inputDbfs);

double goertzelAmplitude(const float* x, size_t n, double freq, double sampleRate);
} // namespace pnam
