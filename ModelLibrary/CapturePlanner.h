#pragma once
// Looks at the control positions that have been captured for one amplifier and says what is
// missing: controls that never moved, gaps between positions, missing ends of a control's
// travel, control pairs that were never moved together (no interaction data), and how many
// captures a good model needs. It then proposes concrete settings to capture next.
#include <string>
#include <vector>

#include "NamFileInfo.h"

namespace pnam
{
struct PlanFinding
{
    enum class Severity { info, warning, blocker };
    Severity severity;
    std::string text;
};

struct PlanSuggestion
{
    std::vector<float> knobs01;
    std::string reason;
};

struct PlanResult
{
    std::vector<PlanFinding> findings;
    std::vector<PlanSuggestion> suggestions;
    int recommendedTotal = 0;
    int captured = 0;
    std::vector<float> base;      // the most common position of each control
    bool trainable = false;       // no blockers
};

PlanResult analyseCapturePlan(const std::vector<KnobInfo>& knobs, const std::vector<std::vector<float>>& captured01,
                              int maxSuggestions = 16);

// "Gain 3.5, Bass 5, Bright on"
std::string describeSetting(const std::vector<KnobInfo>& knobs, const std::vector<float>& v01);
float toDisplay(const KnobInfo& k, float v01);
float fromDisplay(const KnobInfo& k, float display);
} // namespace pnam
