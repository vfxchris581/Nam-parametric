#include "CapturePlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace pnam
{
float toDisplay(const KnobInfo& k, float v)
{
    if (k.isSwitch())
        return std::round(v * (float) (k.labels.size() - 1));
    return k.displayMin + v * (k.displayMax - k.displayMin);
}

float fromDisplay(const KnobInfo& k, float d)
{
    if (k.isSwitch())
        return k.labels.size() > 1 ? std::clamp(d, 0.0f, (float) (k.labels.size() - 1)) / (float) (k.labels.size() - 1) : 0.0f;
    const float span = k.displayMax - k.displayMin;
    return span != 0 ? std::clamp((d - k.displayMin) / span, 0.0f, 1.0f) : 0.0f;
}

static std::string fmtValue(const KnobInfo& k, float v01)
{
    if (k.isSwitch())
        return k.labels[(size_t) std::lround(v01 * (float) (k.labels.size() - 1))];
    char b[32];
    const float d = toDisplay(k, v01);
    std::snprintf(b, sizeof b, std::fabs(d - std::round(d)) < 0.05f ? "%.0f" : "%.1f", d);
    return b;
}

std::string describeSetting(const std::vector<KnobInfo>& knobs, const std::vector<float>& v)
{
    std::string s;
    for (size_t k = 0; k < knobs.size() && k < v.size(); ++k)
        s += (k ? ", " : "") + knobs[k].name + " " + fmtValue(knobs[k], v[k]);
    return s;
}

namespace
{
float quantise(const KnobInfo& k, float v)
{
    if (k.isSwitch())
        return std::round(v * (float) (k.labels.size() - 1)) / (float) (k.labels.size() - 1);
    // dials are set by hand: suggest positions on a 0.5 display step
    const float span = k.displayMax - k.displayMin;
    if (span <= 0)
        return v;
    const float d = std::round(toDisplay(k, v) * 2.0f) / 2.0f;
    return fromDisplay(k, d);
}

bool nameLooksNonlinear(const std::string& n)
{
    std::string s = n;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    for (const char* w : { "gain", "drive", "volume", "master", "boost", "pre", "input" })
        if (s.find(w) != std::string::npos)
            return true;
    return false;
}

double dist2(const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i)
        d += (double) (a[i] - b[i]) * (a[i] - b[i]);
    return d;
}

double halton(int i, int base)
{
    double f = 1, r = 0;
    while (i > 0)
    {
        f /= base;
        r += f * (i % base);
        i /= base;
    }
    return r;
}
} // namespace

PlanResult analyseCapturePlan(const std::vector<KnobInfo>& knobs, const std::vector<std::vector<float>>& caps, int maxSug)
{
    PlanResult r;
    const size_t N = knobs.size();
    r.captured = (int) caps.size();
    using S = PlanFinding::Severity;
    if (N == 0)
    {
        r.findings.push_back({ S::blocker, "Define the amplifier's controls first." });
        return r;
    }
    std::vector<std::vector<float>> all;
    for (const auto& c : caps)
        if (c.size() == N)
            all.push_back(c);

    // base setting: the most common position of each control
    r.base.assign(N, 0.5f);
    for (size_t k = 0; k < N; ++k)
    {
        std::map<int, int> counts;
        for (const auto& c : all)
            counts[(int) std::lround(c[k] * 1000)]++;
        int best = -1, bestN = 0;
        for (auto [v, n] : counts)
            if (n > bestN)
                bestN = n, best = v;
        r.base[k] = best >= 0 ? (float) best / 1000.0f : knobs[k].defaultValue;
    }

    std::vector<PlanSuggestion> sug;
    auto suggest = [&](std::vector<float> v, const std::string& why) {
        for (size_t k = 0; k < N; ++k)
            v[k] = quantise(knobs[k], v[k]);
        for (const auto& c : all)
            if (dist2(c, v) < 1e-6)
                return;
        for (const auto& s : sug)
            if (dist2(s.knobs01, v) < 1e-6)
                return;
        sug.push_back({ v, why });
    };

    // per control coverage
    for (size_t k = 0; k < N; ++k)
    {
        const auto& kn = knobs[k];
        std::vector<float> pos;
        for (const auto& c : all)
            pos.push_back(c[k]);
        std::sort(pos.begin(), pos.end());
        pos.erase(std::unique(pos.begin(), pos.end(), [](float a, float b) { return std::fabs(a - b) < 1e-4f; }), pos.end());

        if (kn.isSwitch())
        {
            for (size_t i = 0; i < kn.labels.size(); ++i)
            {
                const float v = (float) i / (float) (kn.labels.size() - 1);
                if (std::none_of(pos.begin(), pos.end(), [&](float p) { return std::fabs(p - v) < 1e-3f; }))
                {
                    r.findings.push_back({ pos.size() < 2 ? S::blocker : S::warning,
                                           kn.name + ": no capture with the switch at \"" + kn.labels[i] + "\"." });
                    auto v2 = r.base;
                    v2[k] = v;
                    suggest(v2, kn.name + " at \"" + kn.labels[i] + "\" (never captured)");
                }
            }
            continue;
        }
        if (pos.size() < 2)
        {
            r.findings.push_back({ S::blocker, kn.name + " was captured at only " + std::to_string(pos.size())
                                                   + " position: the model cannot learn what it does." });
            for (float v : { 0.0f, 0.5f, 1.0f })
            {
                auto v2 = r.base;
                v2[k] = v;
                suggest(v2, kn.name + " sweep (never moved)");
            }
            continue;
        }
        const bool nonlinear = nameLooksNonlinear(kn.name);
        const size_t want = nonlinear ? 5 : 3;
        if (pos.size() < want)
            r.findings.push_back({ S::warning, kn.name + ": " + std::to_string(pos.size()) + " positions captured; "
                                                   + std::to_string(want) + "+ recommended"
                                                   + (nonlinear ? " (gain-type controls change the sound non-linearly)." : ".") });
        if (pos.front() > 0.15f)
        {
            r.findings.push_back({ S::warning, kn.name + ": lowest capture is " + fmtValue(kn, pos.front())
                                                   + "; below that the model has to extrapolate." });
            auto v2 = r.base;
            v2[k] = 0.0f;
            suggest(v2, kn.name + " minimum (extend range)");
        }
        if (pos.back() < 0.85f)
        {
            r.findings.push_back({ S::warning, kn.name + ": highest capture is " + fmtValue(kn, pos.back())
                                                   + "; above that the model has to extrapolate." });
            auto v2 = r.base;
            v2[k] = 1.0f;
            suggest(v2, kn.name + " maximum (extend range)");
        }
        const float maxGap = nonlinear ? 0.2f : 0.3f;
        for (size_t i = 1; i < pos.size(); ++i)
            if (pos[i] - pos[i - 1] > maxGap + 1e-4f)
            {
                const float mid = 0.5f * (pos[i] + pos[i - 1]);
                r.findings.push_back({ S::info, kn.name + ": gap between " + fmtValue(kn, pos[i - 1]) + " and " + fmtValue(kn, pos[i])
                                                    + "; a capture near " + fmtValue(kn, quantise(kn, mid)) + " would help." });
                auto v2 = r.base;
                v2[k] = mid;
                suggest(v2, kn.name + " fills a gap");
            }
    }

    // interactions: pairs of controls moved away from the base together
    for (size_t i = 0; i < N; ++i)
        for (size_t j = i + 1; j < N; ++j)
        {
            int together = 0;
            for (const auto& c : all)
                if (std::fabs(c[i] - r.base[i]) > 0.05f && std::fabs(c[j] - r.base[j]) > 0.05f)
                    ++together;
            if (together == 0)
            {
                r.findings.push_back({ S::warning, knobs[i].name + " and " + knobs[j].name
                                                       + " were never moved together: the model cannot learn how they interact." });
                for (float a : { 0.15f, 0.85f })
                    for (float b : { 0.15f, 0.85f })
                    {
                        auto v2 = r.base;
                        v2[i] = knobs[i].isSwitch() ? (a < 0.5f ? 0.0f : 1.0f) : a;
                        v2[j] = knobs[j].isSwitch() ? (b < 0.5f ? 0.0f : 1.0f) : b;
                        suggest(v2, knobs[i].name + " x " + knobs[j].name + " interaction");
                    }
            }
            else if (together < 2)
                r.findings.push_back({ S::info, knobs[i].name + " and " + knobs[j].name + " moved together in only one capture." });
        }

    // duplicates
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b)
            if (dist2(all[a], all[b]) < 1e-8)
            {
                r.findings.push_back({ S::info, "Two captures have identical settings (" + describeSetting(knobs, all[a])
                                                    + "); one of them can be used as a hold-out for validation." });
                b = all.size();
                a = all.size();
            }

    // total: a two-level design of all controls is the floor; interaction learning wants more.
    r.recommendedTotal = (int) std::max<size_t>(8, std::min<size_t>(64, 4 * N + 4 + (N > 2 ? 2 * N * (N - 1) / 2 : 0)));
    if ((int) all.size() < r.recommendedTotal)
    {
        r.findings.push_back({ S::info, std::to_string(all.size()) + " captures; about " + std::to_string(r.recommendedTotal)
                                            + " are recommended for " + std::to_string(N) + " controls with interactions." });
        // space-filling extra settings: Halton candidates, greedily farthest from what exists
        std::vector<std::vector<float>> pts = all;
        for (const auto& s : sug)
            pts.push_back(s.knobs01);
        static const int primes[] = { 2, 3, 5, 7, 11, 13, 17, 19 };
        int idx = 1;
        while ((int) (all.size() + sug.size()) < r.recommendedTotal && (int) sug.size() < maxSug)
        {
            std::vector<float> bestV;
            double bestD = -1;
            for (int c = 0; c < 64; ++c, ++idx)
            {
                std::vector<float> v(N);
                for (size_t k = 0; k < N; ++k)
                    v[k] = quantise(knobs[k], (float) halton(idx, primes[k % 8]));
                double dmin = 1e9;
                for (const auto& p : pts)
                    dmin = std::min(dmin, dist2(p, v));
                if (dmin > bestD)
                    bestD = dmin, bestV = v;
            }
            if (bestD < 1e-6)
                break;
            sug.push_back({ bestV, "space-filling setting (interaction coverage)" });
            pts.push_back(bestV);
        }
    }
    if ((int) sug.size() > maxSug)
        sug.resize((size_t) maxSug);
    r.suggestions = sug;
    r.trainable = std::none_of(r.findings.begin(), r.findings.end(), [](const PlanFinding& f) { return f.severity == S::blocker; });
    return r;
}
} // namespace pnam
