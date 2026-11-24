#include "Export.h"

#include <stdexcept>

namespace pnam
{
nlohmann::json exportParametricNam(const TrainableWaveNet& net, const std::vector<KnobInfo>& knobs, double sampleRate,
                                   const nlohmann::json& metadata)
{
    if ((int) knobs.size() != net.numKnobs())
        throw std::runtime_error("export: knob list does not match the network");
    nlohmann::json cfg = net.layout().toConfig();
    nlohmann::json kj = nlohmann::json::array();
    for (const auto& k : knobs)
    {
        nlohmann::json o = { { "name", k.name }, { "default", k.defaultValue } };
        if (k.isSwitch())
            o["labels"] = k.labels;
        else
            o["range"] = { k.displayMin, k.displayMax };
        kj.push_back(o);
    }
    cfg["knobs"] = kj;

    const auto& p = net.params();
    nlohmann::json j;
    j["version"] = "0.5.4";
    j["architecture"] = "ParametricWaveNet";
    j["config"] = cfg;
    j["weights"] = std::vector<float>(p.begin(), p.begin() + (long) net.namWeightCount());
    j["sample_rate"] = sampleRate;
    j["metadata"] = metadata.is_object() ? metadata : nlohmann::json::object();
    if (net.usesKnobScale())
        j["knob_scale"] = net.knobScaleJson();
    return j;
}
} // namespace pnam
