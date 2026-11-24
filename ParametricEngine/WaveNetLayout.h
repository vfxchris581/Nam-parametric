#pragma once
// The NAM WaveNet weight layout (non-gated, no head1x1/bottleneck/groups), shared by the
// snapshot folder, the trainer and the exporter so all three agree on where every weight lives.
//
// Per layer array a (input y, condition c = [audio; knobs]):
//   x   = Wr  (ch x cin) * y                               rechannel, no bias
//   per layer l with dilation d:
//     z    = sum_k W[:,:,k] x[t - (K-1-k) d] + b + M (ch x cs) c   conv + input_mixin
//     post = act(z)
//     x    = x + W1 (ch x ch) post + b1                    1x1 + residual
//     h    = h + post                                      head accumulator (starts at previous array's head)
//   head = Hw (hd x ch) h (+ Hb)                            head rechannel
// y_out = head_scale * head_of_last_array
// Flat order: [Wr, (W, b, M, W1, b1) per layer, Hw, Hb?] per array, then head_scale.
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

namespace pnam
{
struct LayerArraySpec
{
    int inputSize = 1, conditionSize = 1, channels = 16, headSize = 8, kernelSize = 3;
    std::vector<int> dilations;
    bool headBias = false;
    std::string activation = "Tanh";
};

struct WaveNetLayout
{
    std::vector<LayerArraySpec> arrays;
    float headScale = 0.02f;

    struct LayerOffsets { size_t conv, bias, mixin, w1, b1; };
    struct ArrayOffsets { size_t rechannel; std::vector<LayerOffsets> layers; size_t headW, headB; };
    std::vector<ArrayOffsets> offsets;
    size_t headScaleIndex = 0;
    size_t weightCount = 0;

    void computeOffsets()
    {
        offsets.clear();
        size_t p = 0;
        for (size_t a = 0; a < arrays.size(); ++a)
        {
            const auto& L = arrays[a];
            if (a > 0 && arrays[a - 1].headSize != L.channels)
                throw std::runtime_error("layer array " + std::to_string(a) + ": channels must equal previous head_size");
            if (a > 0 && arrays[a - 1].channels != L.inputSize)
                throw std::runtime_error("layer array " + std::to_string(a) + ": input_size must equal previous channels");
            ArrayOffsets ao;
            ao.rechannel = p;
            p += (size_t) (L.channels * L.inputSize);
            for (size_t l = 0; l < L.dilations.size(); ++l)
            {
                LayerOffsets lo;
                lo.conv = p; p += (size_t) (L.channels * L.channels * L.kernelSize);
                lo.bias = p; p += (size_t) L.channels;
                lo.mixin = p; p += (size_t) (L.channels * L.conditionSize);
                lo.w1 = p; p += (size_t) (L.channels * L.channels);
                lo.b1 = p; p += (size_t) L.channels;
                ao.layers.push_back(lo);
            }
            ao.headW = p; p += (size_t) (L.headSize * L.channels);
            ao.headB = p; if (L.headBias) p += (size_t) L.headSize;
            offsets.push_back(ao);
        }
        headScaleIndex = p;
        weightCount = p + 1;
    }

    int receptiveField() const
    {
        int rf = 1;
        for (const auto& L : arrays)
            for (int d : L.dilations)
                rf += (L.kernelSize - 1) * d;
        return rf;
    }

    // Parses a NAM WaveNet config ("layers" array). Rejects features this layout does not cover.
    static WaveNetLayout fromConfig(const nlohmann::json& config)
    {
        WaveNetLayout w;
        if (!config.contains("layers") || !config["layers"].is_array())
            throw std::runtime_error("WaveNet config has no layers");
        if (config.contains("head") && !config["head"].is_null())
            throw std::runtime_error("WaveNet head networks are not supported here");
        for (const auto& L : config["layers"])
        {
            for (const char* k : { "layer1x1", "head1x1", "bottleneck", "groups_input", "groups_input_mixin", "film_params" })
                if (L.contains(k) && !L[k].is_null())
                    throw std::runtime_error(std::string("WaveNet layer key ") + k + " is not supported here");
            if (L.value("gated", false))
                throw std::runtime_error("gated WaveNet layers are not supported here");
            LayerArraySpec s;
            s.inputSize = L.at("input_size").get<int>();
            s.conditionSize = L.at("condition_size").get<int>();
            s.channels = L.at("channels").get<int>();
            s.headSize = L.at("head_size").get<int>();
            s.kernelSize = L.at("kernel_size").get<int>();
            s.dilations = L.at("dilations").get<std::vector<int>>();
            s.headBias = L.value("head_bias", true);
            s.activation = L.at("activation").is_string() ? L["activation"].get<std::string>() : "Tanh";
            w.arrays.push_back(s);
        }
        w.headScale = config.value("head_scale", 0.02f);
        w.computeOffsets();
        return w;
    }

    nlohmann::json toConfig() const
    {
        nlohmann::json layers = nlohmann::json::array();
        for (const auto& L : arrays)
            layers.push_back({ { "input_size", L.inputSize }, { "condition_size", L.conditionSize },
                               { "channels", L.channels }, { "head_size", L.headSize },
                               { "kernel_size", L.kernelSize }, { "dilations", L.dilations },
                               { "activation", L.activation }, { "gated", false }, { "head_bias", L.headBias } });
        return { { "layers", layers }, { "head", nullptr }, { "head_scale", headScale } };
    }
};
} // namespace pnam
