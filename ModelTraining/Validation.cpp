#include "Validation.h"

#include <algorithm>
#include <cmath>

namespace pnam
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

double esr(const std::vector<float>& y, const std::vector<float>& yh, size_t skip)
{
    double num = 0, den = 0;
    const size_t n = std::min(y.size(), yh.size());
    for (size_t i = skip; i < n; ++i)
    {
        const double e = (double) y[i] - (double) yh[i];
        num += e * e;
        den += (double) y[i] * (double) y[i];
    }
    return den > 0 ? num / den : (num > 0 ? 1.0 : 0.0);
}

std::string esrVerdict(double e)
{
    if (e < 0) return "not measured";
    if (e <= 0.01) return "excellent";
    if (e <= 0.03) return "good";
    if (e <= 0.08) return "fair";
    if (e <= 0.2) return "poor";
    return "failed";
}

double goertzelAmplitude(const float* x, size_t n, double freq, double sr)
{
    // Hann-windowed single-bin DFT; amplitude of a sine at `freq`.
    double re = 0, im = 0, wsum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (double) i / (double) (n - 1));
        const double ph = 2 * kPi * freq * (double) i / sr;
        re += w * x[i] * std::cos(ph);
        im -= w * x[i] * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}

static std::vector<float> sine(double freq, double levelDbfs, double seconds, double sr)
{
    const double a = std::pow(10.0, levelDbfs / 20.0);
    std::vector<float> x((size_t) (seconds * sr));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (a * std::sin(2 * kPi * freq * (double) i / sr));
    return x;
}

HarmonicProfile measureHarmonics(const RenderFn& render, double sr, double freq, double levelDbfs, int maxH)
{
    const auto x = sine(freq, levelDbfs, 0.6, sr);
    const auto y = render(x);
    const size_t start = (size_t) (0.2 * sr), n = y.size() - start;
    HarmonicProfile h;
    const double f = goertzelAmplitude(y.data() + start, n, freq, sr);
    h.fundamentalDb = 20.0 * std::log10(std::max(f, 1e-12));
    double sum2 = 0;
    for (int k = 2; k <= maxH && k * freq < sr * 0.5; ++k)
    {
        const double a = goertzelAmplitude(y.data() + start, n, k * freq, sr);
        h.harmonicsDb.push_back(20.0 * std::log10(std::max(a, 1e-12) / std::max(f, 1e-12)));
        sum2 += a * a;
    }
    h.thdPercent = 100.0 * std::sqrt(sum2) / std::max(f, 1e-12);
    return h;
}

std::vector<ResponsePoint> measureResponse(const RenderFn& render, double sr, double levelDbfs, const std::vector<double>& freqsIn)
{
    std::vector<double> freqs = freqsIn;
    if (freqs.empty())
        for (double f = 50; f < std::min(16000.0, sr * 0.45); f *= std::pow(2.0, 1.0 / 3.0))
            freqs.push_back(f);
    std::vector<ResponsePoint> r;
    const double a = std::pow(10.0, levelDbfs / 20.0);
    for (double f : freqs)
    {
        const auto x = sine(f, levelDbfs, 0.25, sr);
        const auto y = render(x);
        const size_t start = (size_t) (0.1 * sr);
        const double g = goertzelAmplitude(y.data() + start, y.size() - start, f, sr) / a;
        r.push_back({ f, 20.0 * std::log10(std::max(g, 1e-12)) });
    }
    return r;
}

std::vector<std::pair<double, double>> measureGainCurve(const RenderFn& render, double sr, const std::vector<double>& in)
{
    std::vector<std::pair<double, double>> out;
    for (double db : in)
    {
        const auto x = sine(1000.0, db, 0.3, sr);
        const auto y = render(x);
        const size_t start = (size_t) (0.1 * sr);
        const double g = goertzelAmplitude(y.data() + start, y.size() - start, 1000.0, sr);
        out.push_back({ db, 20.0 * std::log10(std::max(g, 1e-12)) });
    }
    return out;
}
} // namespace pnam
