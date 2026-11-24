// Capture planning, capture import/recognition, the model library and project files.
#include <filesystem>

#include "CapturePlanner.h"
#include "Libraries.h"
#include "ParametricModel.h"
#include "Snapshot.h"
#include "TestHarness.h"

namespace fs = std::filesystem;
static const fs::path FIX = PNAM_FIXTURES;

static std::vector<pnam::KnobInfo> knobs(std::initializer_list<const char*> names)
{
    std::vector<pnam::KnobInfo> v;
    for (auto n : names) { pnam::KnobInfo k; k.name = n; v.push_back(k); }
    return v;
}

TEST(planner_finds_gain_gaps_like_1_3_5_10)
{
    auto k = knobs({ "Gain" });
    std::vector<std::vector<float>> caps = { { 0.1f }, { 0.3f }, { 0.5f }, { 1.0f } };
    auto r = pnam::analyseCapturePlan(k, caps);
    bool gap = false;
    for (auto& f : r.findings) gap |= f.text.find("gap between 5 and 10") != std::string::npos;
    CHECK(gap);
    bool suggestsMiddle = false;
    for (auto& s : r.suggestions) suggestsMiddle |= s.knobs01[0] > 0.6f && s.knobs01[0] < 0.9f;
    CHECK(suggestsMiddle);
    CHECK(r.trainable);
}

TEST(planner_blocks_unmoved_control_and_flags_missing_interactions)
{
    auto k = knobs({ "Gain", "Bass", "Treble" });
    std::vector<std::vector<float>> caps = { { 0.2f, 0.5f, 0.5f }, { 0.8f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.1f }, { 0.5f, 0.5f, 0.9f } };
    auto r = pnam::analyseCapturePlan(k, caps);
    CHECK(!r.trainable); // Bass never moved
    bool interaction = false;
    for (auto& f : r.findings) interaction |= f.text.find("Gain and Treble were never moved together") != std::string::npos;
    CHECK(interaction);
    CHECK(!r.suggestions.empty());
}

TEST(capture_library_imports_detects_duplicates_and_refuses_parametric)
{
    auto dir = fs::temp_directory_path() / "pnam_lib_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "caps");
    fs::copy_file(FIX / "plain.nam", dir / "caps" / "a.nam");
    fs::copy_file(FIX / "plain.nam", dir / "caps" / "a copy.nam");
    fs::copy_file(FIX / "panama_demo.nam", dir / "caps" / "param.nam");
    pnam::CaptureLibrary lib(dir / "captures.json");
    auto r = lib.importPaths({ dir / "caps" });
    CHECK(r.added.size() == 1);
    CHECK(r.duplicates.size() == 1);
    CHECK(r.parametricSkipped.size() == 1);
    lib.setAmp(r.added[0], "Test Amp");
    lib.save();
    pnam::CaptureLibrary lib2(dir / "captures.json");
    lib2.load();
    CHECK(lib2.entries().size() == 1);
    CHECK(lib2.entries()[0].amp == "Test Amp");
    fs::remove_all(dir);
}

TEST(model_library_only_accepts_parametric_models)
{
    auto dir = fs::temp_directory_path() / "pnam_models_test";
    fs::remove_all(dir);
    pnam::ModelLibrary lib(dir / "Models", dir / "library.json");
    bool refused = false;
    try { lib.addModel(FIX / "plain.nam"); } catch (const std::exception&) { refused = true; }
    CHECK(refused);
    auto& e = lib.addModel(FIX / "panama_demo.nam");
    e.favorite = true;
    e.category = "Crunch";
    e.tags = { "demo" };
    lib.save();
    pnam::ModelLibrary lib2(dir / "Models", dir / "library.json");
    lib2.load();
    CHECK(lib2.entries().size() == 1);
    CHECK(lib2.entries()[0].favorite);
    CHECK(lib2.entries()[0].category == "Crunch");
    CHECK(lib2.entries()[0].info.knobs.size() == 6);
    fs::remove_all(dir);
}

TEST(project_roundtrip)
{
    pnam::StudioProject p;
    p.file = fs::temp_directory_path() / "pnam_proj_test.pnamproj";
    p.ampName = "Plexi";
    p.knobs = knobs({ "Gain", "Bright" });
    p.knobs[1].labels = { "off", "on" };
    p.captures.push_back({ "/tmp/a.nam", "a", { 0.5f, 1.0f }, true, true });
    p.save();
    auto q = pnam::StudioProject::load(p.file);
    CHECK(q.ampName == "Plexi");
    CHECK(q.knobs[1].isSwitch());
    CHECK(q.captures[0].holdout);
    CHECK(q.captures[0].knobs01[1] == 1.0f);
    fs::remove(p.file);
}
