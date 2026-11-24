#pragma once
// Loading NAM files for the two roles they play in the studio:
//   * a PLAYABLE amp model, which must be a ParametricWaveNet (knobs learned by training), and
//   * a SOURCE CAPTURE, an ordinary NAM file used as training data or as an A/B reference.
// A plain NAM file is never presented as a playable parametric model.
#include <filesystem>
#include <memory>
#include <string>

#include <NAM/dsp.h>
#include <json.hpp>

#include "NamFileInfo.h"
#include "ParametricWaveNet.h"

namespace pnam
{
struct PlayableModel
{
    std::unique_ptr<ParametricWaveNet> net;
    NamFileInfo info;
    nlohmann::json json; // the file, kept for snapshot export and presets
};

struct SourceCapture
{
    std::unique_ptr<nam::DSP> dsp;
    NamFileInfo info;
};

// Throws std::runtime_error saying why (e.g. "plain NAM capture: train it into a parametric model first").
PlayableModel loadPlayableModel(const std::filesystem::path& path);
PlayableModel playableModelFromJson(const nlohmann::json& j, const std::filesystem::path& path = {});

// Loads any NAM file NAM core can run (plain captures). Parametric files are refused here.
SourceCapture loadSourceCapture(const std::filesystem::path& path);

nlohmann::json readJsonFile(const std::filesystem::path& path);
void writeJsonFile(const std::filesystem::path& path, const nlohmann::json& j, int indent = -1);

// Runs a mono buffer through a DSP in blocks (offline). Resets the model first.
std::vector<float> renderOffline(nam::DSP& dsp, const std::vector<float>& input, int blockSize = 4096);
} // namespace pnam
