#pragma once
// A ParametricWaveNet at one fixed knob setting is an ordinary NAM WaveNet: the knob columns of
// each input_mixin fold into the conv bias (and knob_scale into the conv weights). This writes
// that plain file, which sounds identical at that setting and loads in any NAM player.
// Method from parametric-nam SPEC.md ("Plain files for players without knob support").
#include <vector>

#include <json.hpp>

namespace pnam
{
nlohmann::json snapshotToPlain(const nlohmann::json& parametric, const std::vector<float>& knobs01);
} // namespace pnam
