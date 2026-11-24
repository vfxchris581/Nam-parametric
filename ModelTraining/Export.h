#pragma once
#include <vector>

#include <json.hpp>

#include "NamFileInfo.h"
#include "TrainableWaveNet.h"

namespace pnam
{
// Writes a trained model as a ParametricWaveNet .nam (parametric-nam SPEC.md v1.1).
// Knob display ranges go in an extra "range" key, which other hosts ignore.
nlohmann::json exportParametricNam(const TrainableWaveNet& net, const std::vector<KnobInfo>& knobs, double sampleRate,
                                   const nlohmann::json& metadata);
} // namespace pnam
