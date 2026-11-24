#include "Trainer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <random>
#include <stdexcept>
#include <thread>

#include <NAM/get_dsp.h>

#include "Export.h"
#include "ParametricModel.h"
#include "TrainableWaveNet.h"
#include "Validation.h"

namespace pnam
{
nlohmann::json ArchitecturePreset::layers(const std::string& name)
{
    auto L = [](int cin, int ch, int hd, std::vector<int> dil, bool hb) {
        return nlohmann::json { { "input_size", cin }, { "condition_size", 1 }, { "channels", ch }, { "head_size", hd },
                                { "kernel_size", 3 }, { "dilations", dil }, { "activation", "Tanh" }, { "gated", false },
                                { "head_bias", hb } };
    };
    const std::vector<int> d10 = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    nlohmann::json cfg;
    if (name == "standard")
        cfg["layers"] = { L(1, 16, 8, d10, false), L(16, 8, 1, d10, true) };
    else if (name == "lite")
        cfg["layers"] = { L(1, 12, 6, { 1, 2, 4, 8, 16, 32, 64 }, false),
                          L(12, 6, 1, { 128, 256, 512, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 }, true) };
    else if (name == "feather")
        cfg["layers"] = { L(1, 8, 4, { 1, 2, 4, 8, 16, 32, 64 }, false),
                          L(8, 4, 1, { 128, 256, 512, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 }, true) };
    else if (name == "test")
        cfg["layers"] = { L(1, 6, 3, { 1, 2, 4, 8, 16, 32, 64 }, false), L(6, 3, 1, { 1, 2, 4, 8, 16, 32, 64 }, true) };
    else
        throw std::runtime_error("unknown architecture preset \"" + name + "\" (standard, lite, feather, test)");
    cfg["head"] = nullptr;
    cfg["head_scale"] = 0.02;
    return cfg;
}

nlohmann::json TrainingReport::toJson() const
{
    nlohmann::json caps = nlohmann::json::array();
    for (const auto& c : captures)
        caps.push_back({ { "label", c.label }, { "knobs", c.knobs01 }, { "holdout", c.holdout }, { "esr", c.esr },
                         { "nearest", c.nearestLabel }, { "nearest_esr", c.nearestEsr } });
    nlohmann::json cv = nlohmann::json::array();
    for (const auto& [s, e] : curve)
        cv.push_back({ s, e });
    return { { "captures", caps }, { "mean_trained_esr", meanTrainedEsr }, { "mean_holdout_esr", meanHoldoutEsr },
             { "mean_nearest_baseline_esr", meanNearestBaselineEsr }, { "validated_on_unseen_settings", validatedOnUnseenSettings },
             { "verdict", verdict }, { "steps", steps }, { "seconds", seconds }, { "curve", cv }, { "warnings", warnings } };
}

std::vector<std::string> checkTrainingConfig(const TrainingConfig& cfg)
{
    std::vector<std::string> p;
    const size_t N = cfg.knobs.size();
    if (N == 0)
        p.push_back("Define at least one control.");
    if (N > 8)
        p.push_back("At most 8 controls per model (format limit).");
    int trained = 0;
    for (const auto& c : cfg.captures)
    {
        if (c.knobs01.size() != N)
            p.push_back(c.label + ": has " + std::to_string(c.knobs01.size()) + " control values, model has " + std::to_string(N));
        for (float v : c.knobs01)
            if (!(v >= 0.0f && v <= 1.0f))
                p.push_back(c.label + ": control values must be inside the control's range");
        if (!c.holdout)
            ++trained;
    }
    if (trained < 2)
        p.push_back("At least two captures (not held out) are needed to learn how a control changes the sound.");
    for (size_t k = 0; k < N; ++k)
    {
        std::vector<float> vals;
        for (const auto& c : cfg.captures)
            if (!c.holdout && c.knobs01.size() == N)
                vals.push_back(c.knobs01[k]);
        std::sort(vals.begin(), vals.end());
        vals.erase(std::unique(vals.begin(), vals.end()), vals.end());
        if (vals.size() < 2)
            p.push_back("Control \"" + cfg.knobs[k].name + "\" has the same position in every training capture: "
                        "the model cannot learn what it does. Capture it at two or more positions.");
    }
    return p;
}

namespace
{
struct Teacher
{
    std::vector<float> y;
    std::vector<float> g;
    bool holdout;
    std::string label;
};

struct Adam
{
    std::vector<float> m, v;
    int t = 0;
    void step(std::vector<float>& p, const std::vector<float>& g, const std::vector<char>& frozen, float lr)
    {
        if (m.empty())
        {
            m.assign(p.size(), 0.0f);
            v.assign(p.size(), 0.0f);
        }
        ++t;
        const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
        const float c1 = 1.0f - std::pow(b1, (float) t), c2 = 1.0f - std::pow(b2, (float) t);
        for (size_t i = 0; i < p.size(); ++i)
        {
            if (frozen[i])
                continue;
            m[i] = b1 * m[i] + (1 - b1) * g[i];
            v[i] = b2 * v[i] + (1 - b2) * g[i] * g[i];
            p[i] -= lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps);
        }
    }
};

// ESR + pre-emphasised ESR; writes dLoss/dOut. Returns plain ESR.
double lossAndGrad(const std::vector<float>& out, const float* y, std::vector<float>& dOut, float preW)
{
    const size_t T = out.size();
    double num = 0, den = 1e-9, nump = 0, denp = 1e-9;
    const float a = 0.95f;
    for (size_t t = 0; t < T; ++t)
    {
        const double e = out[t] - y[t];
        num += e * e;
        den += (double) y[t] * y[t];
        if (t > 0)
        {
            const double ep = e - a * ((double) out[t - 1] - y[t - 1]);
            const double yp = y[t] - a * y[t - 1];
            nump += ep * ep;
            denp += yp * yp;
        }
    }
    dOut.assign(T, 0.0f);
    for (size_t t = 0; t < T; ++t)
    {
        const double e = out[t] - y[t];
        dOut[t] += (float) (2.0 * e / den);
        if (t > 0 && preW > 0)
        {
            const double ep = e - a * ((double) out[t - 1] - y[t - 1]);
            const double gpe = preW * 2.0 * ep / denp;
            dOut[t] += (float) gpe;
            dOut[t - 1] -= (float) (a * gpe);
        }
    }
    return num / den;
}
} // namespace

TrainingResult trainParametric(const TrainingConfig& cfg, const std::function<void(const TrainingProgress&)>& progress,
                               const std::atomic<bool>* cancel)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto report = [&](TrainingProgress p) {
        if (progress)
            progress(p);
    };
    auto cancelled = [&]() { return cancel && cancel->load(); };

    const auto problems = checkTrainingConfig(cfg);
    if (!problems.empty())
    {
        std::string all;
        for (const auto& s : problems)
            all += "\n - " + s;
        throw std::runtime_error("cannot train:" + all);
    }
    const int N = (int) cfg.knobs.size();
    TrainingReport rep;

    // --- 1. Load the source captures and check they can be trained together
    std::vector<std::unique_ptr<nam::DSP>> dsps;
    double sr = -1;
    std::vector<double> inputLevels;
    for (const auto& c : cfg.captures)
    {
        std::unique_ptr<nam::DSP> d;
        NamFileInfo info;
        if (!c.inlineModel.is_null())
        {
            info = namFileInfoFromJson(c.inlineModel);
            d = nam::get_dsp(c.inlineModel);
        }
        else
        {
            auto sc = loadSourceCapture(c.path);
            info = sc.info;
            d = std::move(sc.dsp);
        }
        const double csr = info.sampleRate > 0 ? info.sampleRate : 48000.0;
        if (sr < 0)
            sr = csr;
        else if (std::abs(sr - csr) > 0.5)
            throw std::runtime_error("captures have different sample rates (" + std::to_string((int) sr) + " and "
                                     + std::to_string((int) csr) + " Hz): train captures of one sample rate together");
        if (info.inputLevelDbu)
            inputLevels.push_back(*info.inputLevelDbu);
        dsps.push_back(std::move(d));
    }
    if (!inputLevels.empty())
    {
        auto [mn, mx] = std::minmax_element(inputLevels.begin(), inputLevels.end());
        if (*mx - *mn > 0.5)
            rep.warnings.push_back("The captures were made at different input calibration levels; their gain responses "
                                   "may not line up.");
    }

    // --- 2. Render the training signal through every capture
    TrainingSignalOptions so = cfg.signal;
    so.sampleRate = sr;
    const std::vector<float> x = makeTrainingSignal(so);
    std::vector<Teacher> teachers;
    for (size_t i = 0; i < cfg.captures.size(); ++i)
    {
        if (cancelled())
            throw std::runtime_error("cancelled");
        report({ "rendering", (int) i, (int) cfg.captures.size(), 0, -1, -1, "Rendering " + cfg.captures[i].label });
        Teacher t;
        t.y = renderOffline(*dsps[i], x);
        t.g = cfg.captures[i].knobs01;
        t.holdout = cfg.captures[i].holdout;
        t.label = cfg.captures[i].label.empty() ? ("capture " + std::to_string(i + 1)) : cfg.captures[i].label;
        double e = 0;
        for (float v : t.y)
            e += (double) v * v;
        if (!(e > 0) || !std::isfinite(e))
            throw std::runtime_error(t.label + ": renders silence or invalid output");
        teachers.push_back(std::move(t));
    }
    dsps.clear();

    // --- 3. Network
    nlohmann::json layersCfg = ArchitecturePreset::layers(cfg.architecture);
    TrainableWaveNet net(WaveNetLayout::fromConfig(layersCfg), N, cfg.knobScale);
    net.initialise(cfg.seed);
    const int rf = net.receptiveField();

    // Validation blocks: every 5th 1-second block (all kinds of material are covered).
    const size_t blockLen = (size_t) sr, total = x.size();
    std::vector<char> isVal(total, 0);
    for (size_t b = 0; b * blockLen < total; ++b)
        if (b % 5 == 3)
            std::fill(isVal.begin() + (long) (b * blockLen), isVal.begin() + (long) std::min(total, (b + 1) * blockLen), 1);
    // A training window may not contain validation targets.
    std::vector<size_t> valCum(total + 1, 0);
    for (size_t i = 0; i < total; ++i)
        valCum[i + 1] = valCum[i] + (size_t) isVal[i];
    const int seg = cfg.segment;
    std::vector<size_t> starts;
    for (size_t s = (size_t) rf; s + (size_t) seg <= total; s += 256)
        if (valCum[s + (size_t) seg] == valCum[s])
            starts.push_back(s);
    if (starts.empty())
        throw std::runtime_error("training signal too short for the segment length");
    std::vector<int> trainIdx;
    for (int i = 0; i < (int) teachers.size(); ++i)
        if (!teachers[(size_t) i].holdout)
            trainIdx.push_back(i);

    // validation ranges [a, b) of targets
    std::vector<std::pair<size_t, size_t>> valRanges;
    for (size_t b = 0; b * blockLen < total; ++b)
        if (b % 5 == 3 && b * blockLen >= (size_t) rf)
            valRanges.push_back({ b * blockLen, std::min(total, (b + 1) * blockLen) });

    auto evalEsr = [&](const std::vector<float>& g, const std::vector<float>& y) {
        double num = 0, den = 0;
        std::vector<float> out;
        for (const auto& [a, b] : valRanges)
        {
            const size_t from = a - (size_t) rf + 1;
            net.forward(x.data() + from, (int) (b - from), g.data(), out);
            for (size_t t = 0; t < out.size(); ++t)
            {
                const double e = out[t] - y[a + t];
                num += e * e;
                den += (double) y[a + t] * y[a + t];
            }
        }
        return den > 0 ? num / den : 1.0;
    };
    auto evalMean = [&](bool holdouts) {
        double s = 0;
        int n = 0;
        for (const auto& t : teachers)
            if (t.holdout == holdouts)
            {
                s += evalEsr(t.g, t.y);
                ++n;
            }
        return n ? s / n : -1.0;
    };

    // --- 4. Train
    const int threads = std::max(1, cfg.threads > 0 ? cfg.threads : (int) std::thread::hardware_concurrency());
    const int nThreads = std::min(threads, cfg.batch);
    std::vector<TrainableWaveNet::Workspace> wss((size_t) nThreads);
    std::vector<std::vector<float>> grads((size_t) nThreads, std::vector<float>(net.params().size()));
    std::vector<double> losses((size_t) nThreads);
    std::vector<float> grad(net.params().size());
    std::mt19937 rng(cfg.seed * 7919u + 13u);
    Adam adam;
    std::vector<float> best = net.params();
    double bestVal = 1e30;
    const int warmup = std::min(100, cfg.steps / 10);

    for (int step = 1; step <= cfg.steps; ++step)
    {
        if (cancelled())
            throw std::runtime_error("cancelled");
        std::vector<std::pair<int, size_t>> items((size_t) cfg.batch);
        for (auto& it : items)
            it = { trainIdx[std::uniform_int_distribution<size_t>(0, trainIdx.size() - 1)(rng)],
                   starts[std::uniform_int_distribution<size_t>(0, starts.size() - 1)(rng)] };
        auto work = [&](int th) {
            auto& gth = grads[(size_t) th];
            std::fill(gth.begin(), gth.end(), 0.0f);
            losses[(size_t) th] = 0;
            std::vector<float> out, dOut;
            for (int b = th; b < cfg.batch; b += nThreads)
            {
                const auto& [ti, s] = items[(size_t) b];
                const auto& T = teachers[(size_t) ti];
                const float* xin = x.data() + s - (size_t) rf + 1;
                const int lenX = seg + rf - 1;
                net.forward(xin, lenX, T.g.data(), out, &wss[(size_t) th]);
                losses[(size_t) th] += lossAndGrad(out, T.y.data() + s, dOut, cfg.preEmphasisWeight);
                for (auto& v : dOut)
                    v /= (float) cfg.batch;
                net.backward(xin, lenX, T.g.data(), dOut, wss[(size_t) th], gth);
            }
        };
        std::vector<std::thread> pool;
        for (int th = 1; th < nThreads; ++th)
            pool.emplace_back(work, th);
        work(0);
        for (auto& t : pool)
            t.join();
        std::fill(grad.begin(), grad.end(), 0.0f);
        double loss = 0;
        for (int th = 0; th < nThreads; ++th)
        {
            loss += losses[(size_t) th];
            for (size_t i = 0; i < grad.size(); ++i)
                grad[i] += grads[(size_t) th][i];
        }
        loss /= cfg.batch;
        double gn = 0;
        for (float v : grad)
            gn += (double) v * v;
        gn = std::sqrt(gn);
        if (!std::isfinite(gn))
            continue; // skip a bad batch rather than poisoning the weights
        if (gn > 2.0)
            for (auto& v : grad)
                v *= (float) (2.0 / gn);
        const double prog = (double) step / cfg.steps;
        float lr = cfg.learningRate * (float) (step <= warmup ? (double) step / warmup : 0.05 + 0.95 * 0.5 * (1 + std::cos(M_PI * prog)));
        adam.step(net.params(), grad, net.frozen(), lr);

        if (step % cfg.evalEvery == 0 || step == cfg.steps)
        {
            const double v = evalMean(false);
            const double h = evalMean(true);
            rep.curve.push_back({ step, v });
            if (v < bestVal)
            {
                bestVal = v;
                best = net.params();
            }
            report({ "training", step, cfg.steps, loss, v, h, "" });
        }
        else if (step % 10 == 0)
            report({ "training", step, cfg.steps, loss, -1, -1, "" });
    }
    net.params() = best;

    // --- 5. Validate: every capture at its own setting, on audio not used for training
    report({ "validating", cfg.steps, cfg.steps, 0, -1, -1, "Measuring against every capture" });
    double sumT = 0, sumH = 0, sumB = 0;
    int nT = 0, nH = 0;
    for (const auto& t : teachers)
    {
        CaptureReport cr;
        cr.label = t.label;
        cr.knobs01 = t.g;
        cr.holdout = t.holdout;
        cr.esr = evalEsr(t.g, t.y);
        if (t.holdout)
        {
            // Baseline: playing the nearest captured setting instead of this one.
            double bestD = 1e30;
            const Teacher* nearest = nullptr;
            for (const auto& o : teachers)
                if (!o.holdout)
                {
                    double d = 0;
                    for (int k = 0; k < N; ++k)
                        d += (o.g[(size_t) k] - t.g[(size_t) k]) * (o.g[(size_t) k] - t.g[(size_t) k]);
                    if (d < bestD)
                        bestD = d, nearest = &o;
                }
            if (nearest)
            {
                double num = 0, den = 0;
                for (const auto& [a, b] : valRanges)
                    for (size_t i = a; i < b; ++i)
                    {
                        const double e = (double) nearest->y[i] - t.y[i];
                        num += e * e;
                        den += (double) t.y[i] * t.y[i];
                    }
                cr.nearestLabel = nearest->label;
                cr.nearestEsr = den > 0 ? num / den : -1;
                sumB += cr.nearestEsr;
            }
            sumH += cr.esr;
            ++nH;
        }
        else
        {
            sumT += cr.esr;
            ++nT;
        }
        rep.captures.push_back(cr);
    }
    rep.meanTrainedEsr = nT ? sumT / nT : -1;
    rep.meanHoldoutEsr = nH ? sumH / nH : -1;
    rep.meanNearestBaselineEsr = nH ? sumB / nH : -1;
    rep.validatedOnUnseenSettings = nH > 0;
    rep.steps = cfg.steps;
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!rep.validatedOnUnseenSettings)
        rep.verdict = "Not validated on unseen settings (no hold-out captures). Fit at captured settings: "
                      + esrVerdict(rep.meanTrainedEsr) + ".";
    else
        rep.verdict = "Unseen settings: " + esrVerdict(rep.meanHoldoutEsr) + " (ESR " + std::to_string(rep.meanHoldoutEsr)
                      + "; nearest-capture baseline " + std::to_string(rep.meanNearestBaselineEsr) + "). Captured settings: "
                      + esrVerdict(rep.meanTrainedEsr) + ".";

    // --- 6. Export
    nlohmann::json md = cfg.metadata.is_object() ? cfg.metadata : nlohmann::json::object();
    md["training"] = { { "validation_esr", rep.meanTrainedEsr }, { "holdout_esr", rep.meanHoldoutEsr },
                       { "method", "Parametric NAM Studio capture distillation" }, { "architecture", cfg.architecture },
                       { "report", rep.toJson() } };
    if (!md.contains("name"))
        md["name"] = "Parametric model";
    TrainingResult result;
    result.model = exportParametricNam(net, cfg.knobs, sr, md);
    result.report = rep;
    report({ "done", cfg.steps, cfg.steps, 0, rep.meanTrainedEsr, rep.meanHoldoutEsr, rep.verdict });
    return result;
}
} // namespace pnam
