#include "Snapshot.h"

#include <cstdio>
#include <stdexcept>
#include <string>

#include "WaveNetLayout.h"

namespace pnam
{
nlohmann::json snapshotToPlain(const nlohmann::json& model, const std::vector<float>& g)
{
    const auto arch = model.value("architecture", std::string());
    if (arch != "ParametricWaveNet" && arch != "CoroParametricWaveNet")
        throw std::runtime_error("snapshot: not a ParametricWaveNet");
    const auto& cfg = model.at("config");
    const int N = (int) cfg.at("knobs").size();
    if ((int) g.size() != N)
        throw std::runtime_error("snapshot: " + std::to_string(N) + " knobs, " + std::to_string(g.size()) + " values");
    auto layout = WaveNetLayout::fromConfig(cfg);
    const auto w = model.at("weights").get<std::vector<float>>();
    if (w.size() != layout.weightCount)
        throw std::runtime_error("snapshot: weight count does not match config");
    const bool hasScale = model.contains("knob_scale");
    const auto& ks = hasScale ? model["knob_scale"] : nlohmann::json();

    std::vector<float> out;
    out.reserve(w.size());
    for (size_t a = 0; a < layout.arrays.size(); ++a)
    {
        const auto& L = layout.arrays[a];
        const auto& O = layout.offsets[a];
        const int ch = L.channels, K = L.kernelSize, cs = L.conditionSize;
        if (cs != 1 + N)
            throw std::runtime_error("snapshot: condition_size is not 1 + knobs");
        // rechannel: array 0 drops the knob columns (they must be zero)
        if (a == 0)
        {
            for (int i = 0; i < ch; ++i)
            {
                for (int k = 1; k < L.inputSize; ++k)
                    if (w[O.rechannel + (size_t) (i * L.inputSize + k)] != 0.0f)
                        throw std::runtime_error("snapshot: knob rows feed the input rechannel");
                out.push_back(w[O.rechannel + (size_t) (i * L.inputSize)]);
            }
        }
        else
            out.insert(out.end(), w.begin() + (long) O.rechannel, w.begin() + (long) (O.rechannel + (size_t) (ch * L.inputSize)));

        for (size_t l = 0; l < L.dilations.size(); ++l)
        {
            const auto& lo = O.layers[l];
            std::vector<float> s((size_t) ch, 1.0f);
            if (hasScale)
            {
                const auto& row = ks.at(a).at(l);
                for (int i = 0; i < ch; ++i)
                    for (int k = 0; k < N; ++k)
                        s[(size_t) i] += row.at((size_t) (i * N + k)).get<float>() * g[(size_t) k];
            }
            for (int i = 0; i < ch; ++i)
                for (int j = 0; j < ch * K; ++j)
                    out.push_back(w[lo.conv + (size_t) (i * ch * K + j)] * s[(size_t) i]);
            for (int i = 0; i < ch; ++i)
            {
                double b = w[lo.bias + (size_t) i];
                for (int k = 0; k < N; ++k)
                    b += (double) w[lo.mixin + (size_t) (i * cs + 1 + k)] * g[(size_t) k];
                out.push_back((float) (b * s[(size_t) i]));
            }
            for (int i = 0; i < ch; ++i)
                out.push_back(w[lo.mixin + (size_t) (i * cs)]); // audio column only
            out.insert(out.end(), w.begin() + (long) lo.w1, w.begin() + (long) (lo.b1 + (size_t) ch));
        }
        out.insert(out.end(), w.begin() + (long) O.headW, w.begin() + (long) (O.headW + (size_t) (L.headSize * ch) + (L.headBias ? (size_t) L.headSize : 0)));
    }
    out.push_back(w[layout.headScaleIndex]);

    auto plainLayout = layout;
    for (size_t a = 0; a < plainLayout.arrays.size(); ++a)
    {
        plainLayout.arrays[a].conditionSize = 1;
        if (a == 0)
            plainLayout.arrays[a].inputSize = 1;
    }
    plainLayout.computeOffsets();
    if (plainLayout.weightCount != out.size())
        throw std::runtime_error("snapshot: internal weight count mismatch");

    nlohmann::json plain;
    plain["version"] = model.value("version", std::string("0.5.4"));
    plain["architecture"] = "WaveNet";
    plain["config"] = plainLayout.toConfig();
    plain["weights"] = out;
    if (model.contains("sample_rate"))
        plain["sample_rate"] = model["sample_rate"];
    nlohmann::json md = model.value("metadata", nlohmann::json::object());
    std::string setting;
    for (int k = 0; k < N; ++k)
    {
        char b[64];
        std::snprintf(b, sizeof b, "%s%s %.2f", k ? ", " : "", cfg["knobs"][(size_t) k].value("name", std::string("?")).c_str(), g[(size_t) k]);
        setting += b;
    }
    md["name"] = md.value("name", std::string("Parametric model")) + " @ " + setting;
    md.erase("panel");
    plain["metadata"] = md;
    return plain;
}
} // namespace pnam
