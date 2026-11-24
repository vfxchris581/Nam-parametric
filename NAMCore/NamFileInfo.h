#pragma once
// Reads what a .nam file says about itself without building the model.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

namespace pnam
{
struct KnobInfo
{
    std::string name;
    float defaultValue = 0.5f;           // 0..1
    std::vector<std::string> labels;     // stepped switch positions (empty for a pot)
    float displayMin = 0.0f, displayMax = 10.0f; // how the dial is printed (studio extension, optional)
    bool isSwitch() const { return labels.size() >= 2; }
};

struct NamFileInfo
{
    std::filesystem::path path;
    std::string fileName;
    std::string version;
    std::string architecture;          // "WaveNet", "LSTM", "ParametricWaveNet", ...
    double sampleRate = -1.0;          // -1 when the file does not say (NAM then assumes 48 kHz)
    int weightCount = 0;
    uint64_t weightsHash = 0;          // FNV-1a over the weights: identical models hash the same
    uint64_t fileHash = 0;

    // metadata (all optional in NAM)
    std::string name, modeledBy, gearMake, gearModel, gearType, toneType, date;
    std::optional<double> loudness, inputLevelDbu, outputLevelDbu, validationEsr;

    // architecture summary
    std::string configSummary;         // e.g. "WaveNet 2 arrays, 16/8 ch, RF 4093"
    int receptiveField = 0;

    // parametric
    bool isParametric = false;
    std::vector<KnobInfo> knobs;
    bool hasKnobScale = false;

    // cabinet information from metadata: gear_type contains "cab"
    enum class Cab { unknown, no, yes };
    Cab cabFromMetadata = Cab::unknown;

    std::vector<std::string> warnings; // things a user should know (unknown architecture, no sample rate, ...)
    std::string displayName() const;   // best human name
};

// Parses the file; throws std::runtime_error if it is not a readable NAM JSON file.
NamFileInfo readNamFileInfo(const std::filesystem::path& path);
NamFileInfo namFileInfoFromJson(const nlohmann::json& j, const std::filesystem::path& path = {});

uint64_t fnv1a(const void* data, size_t bytes, uint64_t seed = 1469598103934665603ull);
std::string hashToHex(uint64_t h);

// Architectures NeuralAmpModelerCore can run as plain (non-parametric) captures.
bool isKnownPlainArchitecture(const std::string& architecture);
} // namespace pnam
