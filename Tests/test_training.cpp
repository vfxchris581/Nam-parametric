// The trainer: exact gradients, NAM-core-identical export, and a closed-loop proof that one
// trained model predicts control settings it never saw better than switching snapshots does.
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <random>

#include "Export.h"
#include "ParametricModel.h"
#include "Snapshot.h"
#include "TestHarness.h"
#include "Trainer.h"
#include "TrainableWaveNet.h"
#include "Validation.h"

namespace fs = std::filesystem;
static const fs::path FIX = PNAM_FIXTURES;

static pnam::WaveNetLayout tinyLayout()
{
    nlohmann::json cfg = { { "layers",
                             { { { "input_size", 1 }, { "condition_size", 1 }, { "channels", 3 }, { "head_size", 2 }, { "kernel_size", 3 },
                                 { "dilations", { 1, 2 } }, { "activation", "Tanh" }, { "gated", false }, { "head_bias", false } },
                               { { "input_size", 3 }, { "condition_size", 1 }, { "channels", 2 }, { "head_size", 1 }, { "kernel_size", 3 },
                                 { "dilations", { 1, 4 } }, { "activation", "Tanh" }, { "gated", false }, { "head_bias", true } } } },
                           { "head", nullptr }, { "head_scale", 0.5 } };
    return pnam::WaveNetLayout::fromConfig(cfg);
}

static void randomise(pnam::TrainableWaveNet& net, uint32_t seed)
{
    net.initialise(seed);
    std::mt19937 rng(seed + 1);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    for (size_t i = net.namWeightCount(); i < net.params().size(); ++i)
        net.params()[i] = u(rng); // non-zero knob_scale so that path is exercised
}

TEST(trainer_gradients_match_finite_differences)
{
    for (bool scale : { false, true })
    {
        pnam::TrainableWaveNet net(tinyLayout(), 2, scale);
        randomise(net, 3);
        const int rf = net.receptiveField(), T = 17, lenX = T + rf - 1;
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> u(-0.8f, 0.8f);
        std::vector<float> x((size_t) lenX), r((size_t) T), g = { 0.3f, 0.8f };
        for (auto& v : x) v = u(rng);
        for (auto& v : r) v = u(rng);
        auto lossOf = [&]() {
            std::vector<float> out;
            net.forward(x.data(), lenX, g.data(), out);
            double s = 0;
            for (int t = 0; t < T; ++t) s += (double) out[(size_t) t] * r[(size_t) t];
            return s;
        };
        pnam::TrainableWaveNet::Workspace ws;
        std::vector<float> out, grad(net.params().size(), 0.0f);
        net.forward(x.data(), lenX, g.data(), out, &ws);
        net.backward(x.data(), lenX, g.data(), r, ws, grad);
        int checked = 0;
        double worst = 0;
        for (size_t i = 0; i < net.params().size(); ++i)
        {
            if (net.frozen()[i]) continue;
            const float keep = net.params()[i];
            const float h = 1e-2f;
            net.params()[i] = keep + h; const double lp = lossOf();
            net.params()[i] = keep - h; const double lm = lossOf();
            net.params()[i] = keep;
            const double fd = (lp - lm) / (2 * h);
            const double err = std::fabs(fd - grad[i]) / std::max(1e-2, std::fabs(fd) + std::fabs(grad[i]));
            worst = std::max(worst, err);
            ++checked;
        }
        std::printf("    knob_scale=%d: %d parameters checked, worst relative error %.2e\n", (int) scale, checked, worst);
        CHECK(worst < 2e-2);
    }
}

TEST(trained_layout_runs_identically_in_nam_core)
{
    for (bool scale : { false, true })
    {
        nlohmann::json cfg = pnam::ArchitecturePreset::layers("test");
        pnam::TrainableWaveNet net(pnam::WaveNetLayout::fromConfig(cfg), 3, scale);
        randomise(net, 11);
        std::vector<pnam::KnobInfo> knobs(3);
        knobs[0].name = "Gain"; knobs[1].name = "Bass"; knobs[2].name = "Treble";
        auto j = pnam::exportParametricNam(net, knobs, 48000.0, { { "name", "export test" } });
        auto pm = pnam::playableModelFromJson(j);
        const std::vector<float> g = { 0.2f, 0.7f, 0.45f };
        for (int k = 0; k < 3; ++k) pm.net->setKnob(k, g[(size_t) k]);
        std::mt19937 rng(2);
        std::normal_distribution<float> n(0.0f, 0.2f);
        std::vector<float> x(6000);
        for (auto& v : x) v = n(rng);
        auto yNam = pnam::renderOffline(*pm.net, x, 256);
        const int rf = net.receptiveField();
        std::vector<float> xp((size_t) rf - 1, 0.0f);
        xp.insert(xp.end(), x.begin(), x.end());
        std::vector<float> yOurs;
        net.forward(xp.data(), (int) xp.size(), g.data(), yOurs);
        double m = 0;
        for (size_t i = 0; i < x.size(); ++i) m = std::max(m, (double) std::fabs(yNam[i] - yOurs[i]));
        std::printf("    knob_scale=%d: max |NAM core - trainer| = %.3g\n", (int) scale, m);
        CHECK(m < 1e-4);
    }
}

// Closed loop with known ground truth: "captures" of an amp are plain NAM snapshots of a
// reference parametric model at a grid of two controls. We train one parametric model from the
// captures and compare it, at settings that were held out, with the true sound there.
TEST(parametric_training_predicts_unseen_settings)
{
    const char* fast = std::getenv("PNAM_FAST_TESTS");
    auto ref = pnam::readJsonFile(FIX / "panama_demo.nam");
    // K0 of the reference behaves like a gain/volume control, K2 like a tone control.
    auto knobsAt = [](float a, float b) { return std::vector<float> { a, 0.5f, b, 0.5f, 0.5f, 0.5f }; };
    pnam::TrainingConfig cfg;
    cfg.knobs.resize(2);
    cfg.knobs[0].name = "Gain";
    cfg.knobs[1].name = "Tone";
    const float gainGrid[] = { 0.3f, 0.65f, 1.0f }, toneGrid[] = { 0.0f, 0.5f, 1.0f };
    for (float a : gainGrid)
        for (float b : toneGrid)
        {
            pnam::CaptureSpec c;
            c.inlineModel = pnam::snapshotToPlain(ref, knobsAt(a, b));
            c.knobs01 = { a, b };
            c.label = "G" + std::to_string(a).substr(0, 4) + " T" + std::to_string(b).substr(0, 4);
            cfg.captures.push_back(c);
        }
    for (auto [a, b] : { std::pair { 0.45f, 0.25f }, std::pair { 0.85f, 0.75f }, std::pair { 0.5f, 0.6f } })
    {
        pnam::CaptureSpec c;
        c.inlineModel = pnam::snapshotToPlain(ref, knobsAt(a, b));
        c.knobs01 = { a, b };
        c.holdout = true;
        c.label = "held-out G" + std::to_string(a).substr(0, 4) + " T" + std::to_string(b).substr(0, 4);
        cfg.captures.push_back(c);
    }
    cfg.architecture = fast ? "test" : "feather";
    cfg.steps = fast ? 150 : 3000;
    cfg.batch = 8;
    cfg.segment = 4096;
    cfg.learningRate = 6e-3f;
    cfg.evalEvery = fast ? 50 : 250;
    cfg.signal.seconds = fast ? 12 : 30;
    auto res = pnam::trainParametric(cfg, [](const pnam::TrainingProgress& p) {
        if (p.stage == "training" && p.valEsr >= 0)
            std::printf("    step %4d  loss %.4f  val ESR %.4f  held-out ESR %.4f\n", p.step, p.loss, p.valEsr, p.holdoutEsr);
    });
    const auto& r = res.report;
    for (const auto& c : r.captures)
        if (c.holdout)
            std::printf("    %-22s model ESR %.4f   nearest capture (%s) ESR %.4f\n", c.label.c_str(), c.esr, c.nearestLabel.c_str(), c.nearestEsr);
    std::printf("    mean: trained %.4f, held-out %.4f, nearest-capture baseline %.4f (%.0f s)\n", r.meanTrainedEsr, r.meanHoldoutEsr,
                r.meanNearestBaselineEsr, r.seconds);
    CHECK(r.validatedOnUnseenSettings);
    CHECK(std::isfinite(r.meanHoldoutEsr));
    // The model must beat "switch to the nearest snapshot" at settings it never saw.
    // (PNAM_FAST_TESTS trains far too briefly for that; it only checks the pipeline.)
    if (!fast)
    {
        CHECK(r.meanHoldoutEsr < r.meanNearestBaselineEsr);
        CHECK(r.meanHoldoutEsr < 0.2);
    }
    // and the exported file must load and play as a parametric model
    auto pm = pnam::playableModelFromJson(res.model);
    CHECK(pm.net->knobCount() == 2);
}

TEST(training_config_checks_explain_problems)
{
    pnam::TrainingConfig cfg;
    cfg.knobs.resize(2);
    cfg.knobs[0].name = "Gain";
    cfg.knobs[1].name = "Bass";
    pnam::CaptureSpec a, b;
    a.knobs01 = { 0.2f, 0.5f };
    b.knobs01 = { 0.8f, 0.5f };
    cfg.captures = { a, b };
    auto p = pnam::checkTrainingConfig(cfg);
    bool bassFlagged = false;
    for (auto& s : p)
        bassFlagged |= s.find("Bass") != std::string::npos;
    CHECK(bassFlagged);
}
