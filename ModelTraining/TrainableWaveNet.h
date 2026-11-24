#pragma once
// A NAM-layout WaveNet with knob conditioning that can be TRAINED: forward pass with cached
// activations and an exact backward pass (verified against finite differences in Tests).
//
// The weights live in one flat vector in the exact NAM order (WaveNetLayout), so exporting a
// trained model is writing that vector into a ParametricWaveNet .nam file.
//
// Conditioning (parametric-nam SPEC.md, PANAMA-style): every layer's input_mixin sees
// [audio, knob_1..knob_N]; the knob columns shift each channel's pre-activation. Optionally
// ("knob_scale", spec v1.1) the knobs also scale each channel (FiLM-like), which lets a knob
// act multiplicatively, e.g. a gain control driving later stages harder.
#include <random>
#include <vector>

#include <Eigen/Dense>

#include "WaveNetLayout.h"

namespace pnam
{
class TrainableWaveNet
{
public:
    TrainableWaveNet(WaveNetLayout layout, int numKnobs, bool useKnobScale);

    const WaveNetLayout& layout() const { return layout_; }
    int numKnobs() const { return numKnobs_; }
    bool usesKnobScale() const { return useKnobScale_; }
    int receptiveField() const { return layout_.receptiveField(); }

    // All trainable numbers: NAM weights followed by knob_scale rows (if used).
    std::vector<float>& params() { return params_; }
    const std::vector<float>& params() const { return params_; }
    size_t namWeightCount() const { return layout_.weightCount; }
    // Entries that must stay fixed (head_scale; knob columns of the first rechannel).
    const std::vector<char>& frozen() const { return frozen_; }

    void initialise(uint32_t seed);

    // knob_scale entries of layer l in array a: channels*N floats, row-major (channel, knob)
    size_t knobScaleOffset(size_t a, size_t l) const { return scaleOffsets_[a][l]; }

    struct Workspace; // per-thread activation cache

    // x has length T + receptiveField() - 1; returns the T outputs aligned with x's last T samples.
    // When ws is given, activations are cached for backward().
    void forward(const float* x, int lenX, const float* knobs, std::vector<float>& out, Workspace* ws = nullptr) const;
    // dOut: dLoss/dout for the T outputs of the last forward(ws). Accumulates into grad (params-sized).
    void backward(const float* x, int lenX, const float* knobs, const std::vector<float>& dOut, Workspace& ws,
                  std::vector<float>& grad) const;

    // NAM "knob_scale" JSON (only when usesKnobScale()).
    nlohmann::json knobScaleJson() const;

private:
    WaveNetLayout layout_;
    int numKnobs_;
    bool useKnobScale_;
    std::vector<float> params_;
    std::vector<char> frozen_;
    std::vector<std::vector<size_t>> scaleOffsets_;
};

struct TrainableWaveNet::Workspace
{
    struct Layer
    {
        Eigen::MatrixXf X;   // input to the layer (ch x Tin)
        Eigen::MatrixXf U;   // conv + bias + knob shift (ch x T), before knob scale
        Eigen::MatrixXf P;   // activation output (ch x T)
        Eigen::VectorXf s;   // knob scale per channel
    };
    struct Array
    {
        std::vector<Layer> layers;
        Eigen::MatrixXf Y;      // array input (cin x Tin)
        Eigen::MatrixXf Xout;   // output of last layer (ch x T)
        Eigen::MatrixXf A;      // head accumulator after the last layer (ch x T)
        Eigen::MatrixXf Head;   // head output (hd x T)
    };
    std::vector<Array> arrays;
};
} // namespace pnam
