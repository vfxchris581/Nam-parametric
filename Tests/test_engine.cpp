// Real-time engine: sample-rate conversion, latency, model switching, control smoothing,
// preset recall and CPU cost.
#include <chrono>
#include <cmath>
#include <filesystem>

#include "ModelProcessor.h"
#include "ParametricModel.h"
#include "Presets.h"
#include "TestHarness.h"
#include "Validation.h"
#include "WavIO.h"
#include "dsp/Resampler.h"

namespace fs = std::filesystem;
static const fs::path FIX = PNAM_FIXTURES;

static std::unique_ptr<pnam::ModelSlot> slotFor(pnam::ModelProcessor& mp, const fs::path& p)
{
    auto info = pnam::readNamFileInfo(p);
    if (info.isParametric)
    {
        auto m = pnam::loadPlayableModel(p);
        auto* raw = m.net.get();
        return mp.makeSlot(std::unique_ptr<nam::DSP>(m.net.release()), m.info, raw);
    }
    auto c = pnam::loadSourceCapture(p);
    return mp.makeSlot(std::move(c.dsp), c.info, nullptr);
}

TEST(resampler_passband_stopband_and_latency)
{
    for (auto [a, b] : { std::pair { 44100.0, 48000.0 }, std::pair { 96000.0, 48000.0 }, std::pair { 192000.0, 48000.0 }, std::pair { 88200.0, 48000.0 } })
    {
        // round trip host -> 48k -> host with identity in the middle
        pnam::ResampledProcessor rp;
        rp.prepare(a, b, 256);
        auto render = [&](const std::vector<float>& x) {
            std::vector<float> y(x.size());
            for (size_t p = 0; p < x.size(); p += 256)
            {
                const int n = (int) std::min<size_t>(256, x.size() - p);
                rp.process(x.data() + p, y.data() + p, n, [](float*, int) {});
            }
            return y;
        };
        rp.reset();
        auto resp = pnam::measureResponse(render, a, -6.0, { 100, 1000, 10000, 18000 });
        double worst = 0;
        for (auto& r : resp)
            worst = std::max(worst, std::fabs(r.gainDb));
        // impulse -> latency
        rp.reset();
        std::vector<float> imp(4096, 0.0f);
        imp[100] = 1.0f;
        auto y = render(imp);
        size_t pk = 0;
        for (size_t i = 0; i < y.size(); ++i)
            if (std::fabs(y[i]) > std::fabs(y[pk])) pk = i;
        std::printf("    %6.0f -> %5.0f -> %6.0f Hz: passband dev %.2f dB (to 18 kHz), latency %d (measured %d), underruns %d\n",
                    a, b, a, worst, rp.latencySamples(), (int) pk - 100, rp.underruns());
        CHECK(worst < 0.5);
        CHECK(std::abs((int) pk - 100 - rp.latencySamples()) <= 2);
        CHECK(rp.underruns() == 0);
    }
    // aliasing: a 30 kHz tone at 96 kHz must be removed when going to 48 kHz
    pnam::StreamResampler down;
    down.prepare(96000, 48000, 1024);
    std::vector<float> x(96000), y(60000);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin(2 * M_PI * 30000.0 * i / 96000.0);
    int n = 0;
    for (size_t p = 0; p < x.size(); p += 1024)
        n += down.process(x.data() + p, (int) std::min<size_t>(1024, x.size() - p), y.data() + n, (int) y.size() - n);
    const double alias = pnam::goertzelAmplitude(y.data() + 4000, (size_t) n - 8000, 18000.0, 48000.0);
    std::printf("    30 kHz at 96k -> 48k: alias at 18 kHz %.1f dB\n", 20 * std::log10(alias + 1e-12));
    CHECK(20 * std::log10(alias + 1e-12) < -70);
}

TEST(engine_runs_model_at_its_own_rate_for_every_host_rate)
{
    for (double hostRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        pnam::ModelProcessor mp;
        mp.prepare(hostRate, 256);
        mp.setGate(false, -100);
        mp.setNormalize(false);
        mp.setModel(slotFor(mp, FIX / "panama_demo.nam"));
        pnam::RenderFn render = [&](const std::vector<float>& x) {
            std::vector<float> y(x.size());
            for (size_t p = 0; p < x.size(); p += 256)
                mp.processBlock(x.data() + p, y.data() + p, (int) std::min<size_t>(256, x.size() - p));
            return y;
        };
        render(std::vector<float>(4096, 0.0f)); // take the model
        auto h = pnam::measureHarmonics(render, hostRate, 220.0, -12.0, 5);
        std::printf("    host %6.0f Hz: fundamental %.2f dBFS, THD %.2f%%, latency %d samples\n", hostRate, h.fundamentalDb, h.thdPercent, mp.latencySamples());
        CHECK(std::isfinite(h.fundamentalDb));
        CHECK(h.fundamentalDb > -60);
        if (hostRate == 48000.0)
            CHECK(mp.latencySamples() == 0);
        mp.collectGarbage();
    }
}

TEST(engine_matches_offline_render_at_model_rate)
{
    pnam::ModelProcessor mp;
    mp.prepare(48000, 128);
    mp.setGate(false, -100);
    mp.setNormalize(false);
    auto g = pnam::readJsonFile(FIX / "panama_demo_knobs.json").get<std::vector<float>>();
    for (int k = 0; k < 6; ++k) mp.setKnob(k, g[(size_t) k]);
    mp.setModel(slotFor(mp, FIX / "panama_demo.nam"));
    const auto x = pnam::readWavMono(FIX / "panama_demo_in.wav");
    const auto ref = pnam::readWavMono(FIX / "panama_demo_out.wav");
    // the first block crossfades in from silence; compare after 30 ms
    std::vector<float> y(x.size());
    for (size_t p = 0; p < x.size(); p += 128)
        mp.processBlock(x.data() + p, y.data() + p, (int) std::min<size_t>(128, x.size() - p));
    double m = 0;
    size_t firstBad = 0;
    for (size_t i = 2000; i < x.size(); ++i)
    {
        const double d = std::fabs(y[i] - ref[i]);
        if (d > 1e-5 && !firstBad) firstBad = i;
        m = std::max(m, d);
    }
    if (firstBad) std::printf("    first deviation at sample %zu: %g vs %g\n", firstBad, y[firstBad], ref[firstBad]);
    std::printf("    max |engine - PyTorch| after fade-in = %.3g\n", m);
    CHECK(m < 1e-4);
}

TEST(model_switch_and_knob_moves_are_click_free)
{
    pnam::ModelProcessor mp;
    mp.prepare(48000, 64);
    mp.setGate(false, -100);
    mp.setModel(slotFor(mp, FIX / "panama_demo.nam"));
    const int sr = 48000;
    std::vector<float> x(sr * 2), y(x.size());
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.2f * (float) std::sin(2 * M_PI * 196.0 * i / sr);
    double maxStep = 0, maxStepSteady = 0;
    for (size_t p = 0; p < x.size(); p += 64)
    {
        if (p == (size_t) sr / 2)
            mp.setModel(slotFor(mp, FIX / "plain.nam"));                           // switch model mid-note
        if (p == (size_t) sr)
            for (int k = 0; k < 6; ++k) mp.setKnob(k, k % 2 ? 0.9f : 0.1f);          // yank every control
        if (p == (size_t) (sr * 1.5))
            mp.setModel(slotFor(mp, FIX / "panama_demo.nam"));
        mp.processBlock(x.data() + p, y.data() + p, 64);
    }
    for (size_t i = 1; i < y.size(); ++i)
    {
        const double d = std::fabs(y[i] - y[i - 1]);
        maxStep = std::max(maxStep, d);
        if (i > 4000 && i < (size_t) sr / 2 - 10)
            maxStepSteady = std::max(maxStepSteady, d);
    }
    std::printf("    largest sample-to-sample step: %.4f (steady playing: %.4f)\n", maxStep, maxStepSteady);
    for (float v : y) CHECK(std::isfinite(v));
    // A click is a jump much larger than the waveform itself makes.
    CHECK(maxStep < 3.0 * maxStepSteady + 0.02);
    mp.collectGarbage();
}

TEST(preset_recall_is_exact)
{
    auto dir = fs::temp_directory_path() / "pnam_presets_test";
    fs::remove_all(dir);
    pnam::PresetBank bank(dir);
    pnam::PresetState s;
    s.name = "Lead: 1/2";
    s.modelPath = (FIX / "panama_demo.nam").string();
    s.knobNames = { "K0", "K1" };
    s.knobValues = { 0.123456f, 0.987654f };
    s.inputGainDb = -3.5f;
    s.irA = { "/irs/a.wav", true, -2.0f, -0.5f, false };
    s.irB = { "/irs/b.wav", true, -6.0f, 0.5f, true };
    auto p = bank.save(s);
    auto t = pnam::PresetState::load(p);
    CHECK(t.knobValues == s.knobValues);
    CHECK(t.irB.invert && t.irB.pan == 0.5f);
    CHECK(t.inputGainDb == -3.5f);
    auto d = bank.duplicate(p);
    auto r = bank.rename(d, "Rhythm");
    CHECK(fs::exists(r));
    CHECK(bank.list().size() == 2);
    bank.remove(r);
    CHECK(bank.list().size() == 1);
    fs::remove_all(dir);
}

TEST(cpu_cost_realtime_factor)
{
    pnam::ModelProcessor mp;
    mp.prepare(48000, 128);
    mp.setModel(slotFor(mp, FIX / "panama_demo.nam"));
    std::vector<float> x(48000 * 5), y(x.size());
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.1f * (float) std::sin(i * 0.05);
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t p = 0; p < x.size(); p += 128)
    {
        if (p % 4800 == 0) mp.setKnob(0, (float) (p % 48000) / 48000.0f); // knobs moving all the time
        mp.processBlock(x.data() + p, y.data() + p, 128);
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    5 s of audio (16/8-channel parametric WaveNet, knobs moving) in %.3f s: %.1fx real time\n", s, 5.0 / s);
    CHECK(5.0 / s > 1.0);
}
