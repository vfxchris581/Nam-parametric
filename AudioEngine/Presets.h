#pragma once
// A complete preset: the parametric model, every control position, the cabinet section and the
// signal-chain levels. Stored as JSON (.pnampreset). Control positions are stored by name and
// index, so a preset recalls exactly even if the model file is moved (matched by content hash).
#include <filesystem>
#include <string>
#include <vector>

#include <json.hpp>

namespace pnam
{
struct IRSlotSettings
{
    std::string path;
    bool enabled = false;
    float levelDb = 0.0f;
    float pan = 0.0f;     // -1 left .. +1 right
    bool invert = false;
};

struct PresetState
{
    std::string name;
    std::string modelPath, modelHash, modelName;
    std::vector<std::string> knobNames;
    std::vector<float> knobValues;   // 0..1
    float inputGainDb = 0.0f, outputGainDb = 0.0f;
    bool gateOn = true;
    float gateThresholdDb = -70.0f;
    bool normalizeLoudness = true;
    bool stereo = true;
    IRSlotSettings irA, irB;
    bool cabEnabled = true;

    nlohmann::json toJson() const;
    static PresetState fromJson(const nlohmann::json& j);
    void save(const std::filesystem::path& p) const;
    static PresetState load(const std::filesystem::path& p);
};

class PresetBank
{
public:
    explicit PresetBank(std::filesystem::path dir);
    std::vector<std::filesystem::path> list() const;
    std::filesystem::path pathFor(const std::string& name) const;
    std::filesystem::path save(const PresetState& s);                       // by name; overwrites
    std::filesystem::path rename(const std::filesystem::path& p, const std::string& newName);
    std::filesystem::path duplicate(const std::filesystem::path& p);
    void remove(const std::filesystem::path& p);
    std::filesystem::path importFile(const std::filesystem::path& external);
    static void exportFile(const std::filesystem::path& p, const std::filesystem::path& dest);
    const std::filesystem::path& dir() const { return dir_; }

private:
    std::filesystem::path dir_;
};

std::string sanitiseFileName(const std::string& s);
} // namespace pnam
