// NAM compatibility and parametric loading: these tests compare our C++ playback with
// reference output computed by PyTorch (fixtures from parametric-nam, MIT).
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "NamFileInfo.h"
#include "ParametricModel.h"
#include "Snapshot.h"
#include "TestHarness.h"
#include "WavIO.h"

namespace fs = std::filesystem;
static const fs::path FIX = PNAM_FIXTURES;

static double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        m = std::max(m, (double) std::fabs(a[i] - b[i]));
    return m;
}
static double peak(const std::vector<float>& a)
{
    double m = 0;
    for (float v : a)
        m = std::max(m, (double) std::fabs(v));
    return m;
}

TEST(plain_nam_loads_as_source_capture)
{
    auto cap = pnam::loadSourceCapture(FIX / "plain.nam");
    CHECK(cap.dsp != nullptr);
    CHECK(!cap.info.isParametric);
    CHECK(cap.info.architecture == "WaveNet");
    std::vector<float> x(4800);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.2f * (float) std::sin(2 * M_PI * 220.0 * (double) i / 48000.0);
    auto y = pnam::renderOffline(*cap.dsp, x);
    CHECK(y.size() == x.size());
    for (float v : y)
        CHECK(std::isfinite(v));
}

TEST(plain_nam_is_not_playable_parametric)
{
    bool threw = false;
    try
    {
        pnam::loadPlayableModel(FIX / "plain.nam");
    }
    catch (const std::exception& e)
    {
        threw = std::string(e.what()).find("plain NAM capture") != std::string::npos;
    }
    CHECK(threw);
}

TEST(parametric_info_reads_knobs)
{
    auto info = pnam::readNamFileInfo(FIX / "panama_demo.nam");
    CHECK(info.isParametric);
    CHECK(info.knobs.size() == 6);
    CHECK(info.knobs[0].name == "K0");
    CHECK(info.sampleRate == 48000.0);
    CHECK(info.receptiveField == 4093);
}

TEST(parametric_nulls_against_pytorch)
{
    auto model = pnam::loadPlayableModel(FIX / "panama_demo.nam");
    auto g = pnam::readJsonFile(FIX / "panama_demo_knobs.json").get<std::vector<float>>();
    CHECK(model.net->knobCount() == (int) g.size());
    for (int k = 0; k < model.net->knobCount(); ++k)
        model.net->setKnob(k, g[(size_t) k]);
    const auto x = pnam::readWavMono(FIX / "panama_demo_in.wav");
    const auto ref = pnam::readWavMono(FIX / "panama_demo_out.wav");
    for (int block : { 16, 32, 64, 512, 4096 })
    {
        auto y = pnam::renderOffline(*model.net, x, block);
        const double err = maxAbsDiff(y, ref);
        std::printf("    block %d: max |C++ - PyTorch| = %.3g (ref peak %.3f)\n", block, err, peak(ref));
        CHECK(err < 1e-4 * std::max(1.0, peak(ref)));
    }
}

TEST(snapshot_matches_parametric_at_setting)
{
    auto j = pnam::readJsonFile(FIX / "panama_demo.nam");
    const std::vector<float> g = { 0.1f, 0.9f, 0.3f, 0.6f, 0.0f, 1.0f };
    auto plainJson = pnam::snapshotToPlain(j, g);
    CHECK(plainJson["architecture"] == "WaveNet");
    const auto tmp = fs::temp_directory_path() / "pnam_snapshot_test.nam";
    pnam::writeJsonFile(tmp, plainJson);
    auto plain = pnam::loadSourceCapture(tmp);
    auto para = pnam::playableModelFromJson(j);
    for (int k = 0; k < 6; ++k)
        para.net->setKnob(k, g[(size_t) k]);
    const auto x = pnam::readWavMono(FIX / "panama_demo_in.wav");
    auto a = pnam::renderOffline(*para.net, x);
    auto b = pnam::renderOffline(*plain.dsp, x);
    const double err = maxAbsDiff(a, b);
    std::printf("    max |parametric - snapshot| = %.3g\n", err);
    CHECK(err < 1e-5);
    fs::remove(tmp);
}

TEST(knobs_change_the_sound)
{
    auto model = pnam::loadPlayableModel(FIX / "panama_demo.nam");
    const auto x = pnam::readWavMono(FIX / "panama_demo_in.wav");
    for (int k = 0; k < 6; ++k)
        model.net->setKnob(k, 0.1f);
    auto lo = pnam::renderOffline(*model.net, x);
    for (int k = 0; k < 6; ++k)
        model.net->setKnob(k, 0.9f);
    auto hi = pnam::renderOffline(*model.net, x);
    CHECK(maxAbsDiff(lo, hi) > 1e-3);
}
