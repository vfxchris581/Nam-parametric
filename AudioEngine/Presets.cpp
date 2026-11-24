#include "Presets.h"

#include <algorithm>
#include <stdexcept>

#include "Libraries.h"
#include "ParametricModel.h"

namespace pnam
{
namespace fs = std::filesystem;

static nlohmann::json irJson(const IRSlotSettings& s)
{
    return { { "path", s.path }, { "enabled", s.enabled }, { "level_db", s.levelDb }, { "pan", s.pan }, { "invert", s.invert } };
}
static IRSlotSettings irFrom(const nlohmann::json& j)
{
    IRSlotSettings s;
    if (!j.is_object())
        return s;
    s.path = j.value("path", std::string());
    s.enabled = j.value("enabled", false);
    s.levelDb = j.value("level_db", 0.0f);
    s.pan = j.value("pan", 0.0f);
    s.invert = j.value("invert", false);
    return s;
}

nlohmann::json PresetState::toJson() const
{
    return { { "format", "pnam-preset" }, { "version", 1 }, { "name", name },
             { "model", { { "path", modelPath }, { "hash", modelHash }, { "name", modelName } } },
             { "controls", { { "names", knobNames }, { "values", knobValues } } },
             { "input_gain_db", inputGainDb }, { "output_gain_db", outputGainDb },
             { "gate", { { "on", gateOn }, { "threshold_db", gateThresholdDb } } },
             { "normalize_loudness", normalizeLoudness }, { "stereo", stereo },
             { "cab", { { "enabled", cabEnabled }, { "a", irJson(irA) }, { "b", irJson(irB) } } } };
}

PresetState PresetState::fromJson(const nlohmann::json& j)
{
    if (j.value("format", std::string()) != "pnam-preset")
        throw std::runtime_error("not a Parametric NAM Studio preset");
    PresetState s;
    s.name = j.value("name", std::string());
    const auto m = j.value("model", nlohmann::json::object());
    s.modelPath = m.value("path", std::string());
    s.modelHash = m.value("hash", std::string());
    s.modelName = m.value("name", std::string());
    const auto c = j.value("controls", nlohmann::json::object());
    s.knobNames = c.value("names", std::vector<std::string>());
    s.knobValues = c.value("values", std::vector<float>());
    s.inputGainDb = j.value("input_gain_db", 0.0f);
    s.outputGainDb = j.value("output_gain_db", 0.0f);
    const auto g = j.value("gate", nlohmann::json::object());
    s.gateOn = g.value("on", true);
    s.gateThresholdDb = g.value("threshold_db", -70.0f);
    s.normalizeLoudness = j.value("normalize_loudness", true);
    s.stereo = j.value("stereo", true);
    const auto cab = j.value("cab", nlohmann::json::object());
    s.cabEnabled = cab.value("enabled", true);
    s.irA = irFrom(cab.value("a", nlohmann::json()));
    s.irB = irFrom(cab.value("b", nlohmann::json()));
    return s;
}

void PresetState::save(const fs::path& p) const { writeJsonFile(p, toJson(), 2); }
PresetState PresetState::load(const fs::path& p) { return fromJson(readJsonFile(p)); }

std::string sanitiseFileName(const std::string& s)
{
    std::string o;
    for (char c : s)
        o += (std::string("\\/:*?\"<>|").find(c) == std::string::npos && (unsigned char) c >= 32) ? c : '_';
    while (!o.empty() && (o.back() == ' ' || o.back() == '.'))
        o.pop_back();
    return o.empty() ? "Preset" : o;
}

PresetBank::PresetBank(fs::path dir) : dir_(std::move(dir)) { fs::create_directories(dir_); }

std::vector<fs::path> PresetBank::list() const
{
    std::vector<fs::path> v;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir_, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->path().extension() == ".pnampreset")
            v.push_back(it->path());
    std::sort(v.begin(), v.end());
    return v;
}

fs::path PresetBank::pathFor(const std::string& name) const { return dir_ / fromUtf8(sanitiseFileName(name) + ".pnampreset"); }

fs::path PresetBank::save(const PresetState& s)
{
    const auto p = pathFor(s.name);
    s.save(p);
    return p;
}

fs::path PresetBank::rename(const fs::path& p, const std::string& newName)
{
    auto s = PresetState::load(p);
    s.name = newName;
    const auto np = pathFor(newName);
    if (np != p && fs::exists(np))
        throw std::runtime_error("a preset called \"" + newName + "\" already exists");
    s.save(np);
    if (np != p)
        fs::remove(p);
    return np;
}

fs::path PresetBank::duplicate(const fs::path& p)
{
    auto s = PresetState::load(p);
    std::string base = s.name + " copy";
    std::string name = base;
    for (int i = 2; fs::exists(pathFor(name)); ++i)
        name = base + " " + std::to_string(i);
    s.name = name;
    return save(s);
}

void PresetBank::remove(const fs::path& p)
{
    std::error_code ec;
    fs::remove(p, ec);
}

fs::path PresetBank::importFile(const fs::path& ext)
{
    auto s = PresetState::load(ext);
    if (s.name.empty())
        s.name = ext.stem().string();
    std::string name = s.name;
    for (int i = 2; fs::exists(pathFor(name)); ++i)
        name = s.name + " " + std::to_string(i);
    s.name = name;
    return save(s);
}

void PresetBank::exportFile(const fs::path& p, const fs::path& dest) { fs::copy_file(p, dest, fs::copy_options::overwrite_existing); }
} // namespace pnam
