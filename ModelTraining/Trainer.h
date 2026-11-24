#pragma once
// Trains ONE control-conditioned network from several NAM captures of the same amplifier.
//
// Method (capture distillation): the dry training signal is rendered through every source
// capture with NAM core, giving one target recording per captured control setting. A single
// ParametricWaveNet then learns  y = f(audio, knobs)  across all of them at once, so it
// predicts settings between and around the captured ones and the interactions between
// controls. Captures marked "hold-out" are never trained on: they measure how well the model
// predicts settings it has not seen, against the nearest-captured-setting baseline.
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <json.hpp>

#include "NamFileInfo.h"
#include "TrainingSignal.h"

namespace pnam
{
struct CaptureSpec
{
    std::filesystem::path path;   // a plain .nam file ...
    nlohmann::json inlineModel;   // ... or the model JSON itself (tests)
    std::vector<float> knobs01;   // this capture's control positions, 0..1, in knob order
    bool holdout = false;
    std::string label;
};

struct ArchitecturePreset
{
    std::string name;             // "standard", "lite", "feather", "test"
    static nlohmann::json layers(const std::string& name);
};

struct TrainingConfig
{
    std::vector<KnobInfo> knobs;
    std::vector<CaptureSpec> captures;
    std::string architecture = "standard";
    bool knobScale = true;
    int steps = 3000;
    int batch = 16;
    int segment = 8192;
    float learningRate = 4e-3f;
    float preEmphasisWeight = 1.0f;
    int threads = 0;              // 0 = hardware concurrency
    uint32_t seed = 1;
    int evalEvery = 100;
    TrainingSignalOptions signal;
    nlohmann::json metadata;      // copied into the model's metadata
};

struct TrainingProgress
{
    std::string stage;            // "rendering", "training", "validating", "done"
    int step = 0, totalSteps = 0;
    double loss = 0, valEsr = -1, holdoutEsr = -1;
    std::string message;
};

struct CaptureReport
{
    std::string label;
    std::vector<float> knobs01;
    bool holdout = false;
    double esr = -1;              // model at this setting vs the capture, on audio not used for training
    std::string nearestLabel;     // hold-outs: the nearest trained capture...
    double nearestEsr = -1;       // ...and its ESR against this capture (what "just switching snapshots" gives)
};

struct TrainingReport
{
    std::vector<CaptureReport> captures;
    double meanTrainedEsr = -1, meanHoldoutEsr = -1, meanNearestBaselineEsr = -1;
    std::vector<std::pair<int, double>> curve; // step -> validation ESR
    bool validatedOnUnseenSettings = false;
    std::string verdict;
    int steps = 0;
    double seconds = 0;
    std::vector<std::string> warnings;
    nlohmann::json toJson() const;
};

struct TrainingResult
{
    nlohmann::json model;         // ParametricWaveNet .nam
    TrainingReport report;
};

// Checks a configuration without training: returns problems that would stop training.
std::vector<std::string> checkTrainingConfig(const TrainingConfig& cfg);

TrainingResult trainParametric(const TrainingConfig& cfg, const std::function<void(const TrainingProgress&)>& progress = {},
                               const std::atomic<bool>* cancel = nullptr);
} // namespace pnam
