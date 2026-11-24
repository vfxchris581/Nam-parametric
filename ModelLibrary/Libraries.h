#pragma once
// The two libraries the app keeps, plus the Capture Studio project file.
//
//  * CaptureLibrary: ordinary .nam captures imported as SOURCE material. They are grouped into
//    amplifiers only from metadata or by the user; similar file names are never treated as proof.
//  * ModelLibrary: the PLAYABLE parametric models. Only ParametricWaveNet files are accepted.
//  * StudioProject: one amplifier being turned into a parametric model: its controls, which
//    capture was made at which control positions, hold-outs, training settings, model versions.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "NamFileInfo.h"

namespace pnam
{
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------- captures
struct CaptureEntry
{
    fs::path path;
    NamFileInfo info;
    std::string amp;              // amplifier group (user-confirmed or from metadata)
    bool ampFromMetadata = false;
    enum class Cab { unknown, no, yes, likely };
    Cab cab = Cab::unknown;       // metadata first, measurement second ("likely")
    std::string cabReason;
    std::vector<std::string> problems;
};

struct ImportResult
{
    std::vector<fs::path> added, duplicates, parametricSkipped;
    std::vector<std::pair<fs::path, std::string>> failed;
};

class CaptureLibrary
{
public:
    explicit CaptureLibrary(fs::path indexFile);
    void load();
    void save() const;

    ImportResult importPaths(const std::vector<fs::path>& filesOrFolders); // folders are scanned recursively
    const std::vector<CaptureEntry>& entries() const { return entries_; }
    CaptureEntry* find(const fs::path& p);
    void setAmp(const fs::path& p, const std::string& amp);
    void remove(const fs::path& p);
    std::vector<std::string> ampNames() const;
    std::vector<const CaptureEntry*> capturesOfAmp(const std::string& amp) const;

    // Suggestions only: metadata gear make/model when present. Never from file names alone.
    static std::string ampFromMetadata(const NamFileInfo& info);
    // Measures whether a capture sounds like it already contains a speaker cabinet.
    static CaptureEntry::Cab measureCab(const fs::path& p, std::string* reason);
    // Problems within one amp group (mixed sample rates, calibration levels, cab/no-cab mix).
    static std::vector<std::string> groupProblems(const std::vector<const CaptureEntry*>& group);

private:
    fs::path index_;
    std::vector<CaptureEntry> entries_;
};

// ---------------------------------------------------------------------------- models
struct ModelEntry
{
    fs::path path;
    NamFileInfo info;
    std::string name, manufacturer, amp, channel, description, creator, cabinet;
    std::string category; // Clean, Crunch, Overdrive, High Gain, Metal, Vintage, Boutique or custom
    std::vector<std::string> tags, collections;
    bool favorite = false;
    std::string validation; // short verdict from the training report
    nlohmann::json toJson() const;
    static ModelEntry fromJson(const nlohmann::json& j);
};

class ModelLibrary
{
public:
    ModelLibrary(fs::path modelsDir, fs::path indexFile);
    void load();
    void save() const;
    // Copies a ParametricWaveNet file into the library. Throws for plain NAM captures.
    ModelEntry& addModel(const fs::path& file, bool copyIntoLibrary = true);
    ModelEntry& addModelJson(const nlohmann::json& model, const std::string& fileStem);
    void rescan();
    std::vector<ModelEntry>& entries() { return entries_; }
    const std::vector<ModelEntry>& entries() const { return entries_; }
    ModelEntry* find(const fs::path& p);
    void remove(const fs::path& p, bool deleteFile);
    static const std::vector<std::string>& standardCategories();
    const fs::path& modelsDir() const { return dir_; }

private:
    fs::path dir_, index_;
    std::vector<ModelEntry> entries_;
};

// ---------------------------------------------------------------------------- projects
struct ProjectCapture
{
    fs::path path;
    std::string label;
    std::vector<float> knobs01;
    bool holdout = false;
    bool assigned = false; // the user has entered its control positions
};

struct ModelVersion
{
    int number = 0;
    fs::path file;
    std::string date, architecture, verdict;
    double trainedEsr = -1, holdoutEsr = -1, baselineEsr = -1;
};

struct StudioProject
{
    fs::path file;
    std::string ampName, manufacturer, channel, description, creator, category;
    std::vector<KnobInfo> knobs;
    std::vector<ProjectCapture> captures;
    std::string architecture = "standard";
    int steps = 3000;
    bool knobScale = true;
    double signalSeconds = 90.0;
    std::vector<fs::path> diFiles;
    std::vector<ModelVersion> versions;

    nlohmann::json toJson() const;
    static StudioProject fromJson(const nlohmann::json& j, const fs::path& file = {});
    void save() const;
    static StudioProject load(const fs::path& file);
};

// App folders: ~/Library/Application Support/ParametricNAMStudio (macOS),
// %APPDATA%/ParametricNAMStudio (Windows), ~/.local/share/ParametricNAMStudio (Linux).
fs::path appDataDir();
std::string toUtf8(const fs::path& p);
fs::path fromUtf8(const std::string& s);
std::string todayString();
} // namespace pnam
