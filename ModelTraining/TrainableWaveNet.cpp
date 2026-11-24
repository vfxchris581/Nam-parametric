#include "TrainableWaveNet.h"

#include <cmath>
#include <stdexcept>

namespace pnam
{
using RowMat = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using CMap = Eigen::Map<const RowMat>;
using MMap = Eigen::Map<RowMat>;
using StrideD = Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>;
using CConvMap = Eigen::Map<const Eigen::MatrixXf, 0, StrideD>;
using MConvMap = Eigen::Map<Eigen::MatrixXf, 0, StrideD>;

namespace
{
// Conv weights are stored [out][in][k]; this views tap k as a (ch x ch) column-major matrix.
CConvMap convTap(const float* base, int ch, int K, int k)
{
    return CConvMap(base + k, ch, ch, StrideD(K, ch * K));
}
MConvMap convTap(float* base, int ch, int K, int k)
{
    return MConvMap(base + k, ch, ch, StrideD(K, ch * K));
}
} // namespace

TrainableWaveNet::TrainableWaveNet(WaveNetLayout layout, int numKnobs, bool useKnobScale)
    : layout_(std::move(layout)), numKnobs_(numKnobs), useKnobScale_(useKnobScale)
{
    if (layout_.arrays.empty())
        throw std::runtime_error("TrainableWaveNet: no layer arrays");
    for (size_t a = 0; a < layout_.arrays.size(); ++a)
    {
        auto& L = layout_.arrays[a];
        L.conditionSize = 1 + numKnobs;
        if (a == 0)
            L.inputSize = 1 + numKnobs;
        if (L.activation != "Tanh")
            throw std::runtime_error("TrainableWaveNet: only Tanh activations are trainable");
    }
    layout_.computeOffsets();
    size_t n = layout_.weightCount;
    scaleOffsets_.resize(layout_.arrays.size());
    if (useKnobScale_)
        for (size_t a = 0; a < layout_.arrays.size(); ++a)
            for (size_t l = 0; l < layout_.arrays[a].dilations.size(); ++l)
            {
                scaleOffsets_[a].push_back(n);
                n += (size_t) (layout_.arrays[a].channels * numKnobs);
            }
    params_.assign(n, 0.0f);
    frozen_.assign(n, 0);
    frozen_[layout_.headScaleIndex] = 1;
    const auto& L0 = layout_.arrays[0];
    for (int i = 0; i < L0.channels; ++i)
        for (int k = 1; k < L0.inputSize; ++k)
            frozen_[layout_.offsets[0].rechannel + (size_t) (i * L0.inputSize + k)] = 1;
}

void TrainableWaveNet::initialise(uint32_t seed)
{
    std::mt19937 rng(seed);
    auto fill = [&](size_t off, size_t n, float bound) {
        std::uniform_real_distribution<float> u(-bound, bound);
        for (size_t i = 0; i < n; ++i)
            params_[off + i] = u(rng);
    };
    std::fill(params_.begin(), params_.end(), 0.0f);
    for (size_t a = 0; a < layout_.arrays.size(); ++a)
    {
        const auto& L = layout_.arrays[a];
        const auto& O = layout_.offsets[a];
        const int ch = L.channels, K = L.kernelSize, cs = L.conditionSize;
        if (a == 0)
            for (int i = 0; i < ch; ++i)
                params_[O.rechannel + (size_t) (i * L.inputSize)] = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
        else
            fill(O.rechannel, (size_t) (ch * L.inputSize), 1.0f / std::sqrt((float) L.inputSize));
        for (const auto& lo : O.layers)
        {
            const float bc = 1.0f / std::sqrt((float) (ch * K));
            fill(lo.conv, (size_t) (ch * ch * K), bc);
            fill(lo.bias, (size_t) ch, bc);
            std::uniform_real_distribution<float> ua(-1.0f, 1.0f), uk(-0.3f, 0.3f);
            for (int i = 0; i < ch; ++i)
            {
                params_[lo.mixin + (size_t) (i * cs)] = ua(rng);
                for (int k = 1; k < cs; ++k)
                    params_[lo.mixin + (size_t) (i * cs + k)] = uk(rng);
            }
            fill(lo.w1, (size_t) (ch * ch), 1.0f / std::sqrt((float) ch));
            fill(lo.b1, (size_t) ch, 1.0f / std::sqrt((float) ch));
        }
        fill(O.headW, (size_t) (L.headSize * ch), 1.0f / std::sqrt((float) ch));
        if (L.headBias)
            fill(O.headB, (size_t) L.headSize, 1.0f / std::sqrt((float) ch));
    }
    params_[layout_.headScaleIndex] = layout_.headScale;
}

void TrainableWaveNet::forward(const float* x, int lenX, const float* g, std::vector<float>& out, Workspace* ws) const
{
    const int N = numKnobs_;
    Eigen::Map<const Eigen::RowVectorXf> xin(x, lenX);
    Eigen::Map<const Eigen::VectorXf> gv(g, N);
    const float* w = params_.data();
    if (ws)
        ws->arrays.resize(layout_.arrays.size());

    Eigen::MatrixXf Y, headPrev;
    int Tin = lenX;
    for (size_t a = 0; a < layout_.arrays.size(); ++a)
    {
        const auto& L = layout_.arrays[a];
        const auto& O = layout_.offsets[a];
        const int ch = L.channels, K = L.kernelSize, cs = L.conditionSize;
        Workspace::Array* wa = ws ? &ws->arrays[a] : nullptr;
        if (wa)
            wa->layers.resize(L.dilations.size());

        Eigen::MatrixXf X;
        if (a == 0)
        {
            Eigen::VectorXf wr0(ch);
            for (int i = 0; i < ch; ++i)
                wr0[i] = w[O.rechannel + (size_t) (i * L.inputSize)];
            X = wr0 * xin;
        }
        else
        {
            X.noalias() = CMap(w + O.rechannel, ch, L.inputSize) * Y;
            if (wa)
                wa->Y = Y;
        }

        Eigen::MatrixXf A;
        for (size_t l = 0; l < L.dilations.size(); ++l)
        {
            const auto& lo = O.layers[l];
            const int d = L.dilations[l];
            const int T = Tin - (K - 1) * d;
            if (T <= 0)
                throw std::runtime_error("TrainableWaveNet::forward: input shorter than receptive field");
            CMap M(w + lo.mixin, ch, cs);
            Eigen::VectorXf c = Eigen::Map<const Eigen::VectorXf>(w + lo.bias, ch);
            if (N > 0)
                c.noalias() += M.rightCols(N) * gv;
            Eigen::MatrixXf U = c.replicate(1, T);
            for (int k = 0; k < K; ++k)
                U.noalias() += convTap(w + lo.conv, ch, K, k) * X.middleCols(k * d, T);
            Eigen::VectorXf s = Eigen::VectorXf::Ones(ch);
            if (useKnobScale_ && N > 0)
                s.noalias() += CMap(w + scaleOffsets_[a][l], ch, N) * gv;
            Eigen::MatrixXf Z = s.asDiagonal() * U;
            Z.noalias() += M.col(0) * xin.tail(T);
            Eigen::MatrixXf P = Z.array().tanh().matrix();
            Eigen::MatrixXf Xn = X.rightCols(T);
            Xn.noalias() += CMap(w + lo.w1, ch, ch) * P;
            Xn.colwise() += Eigen::Map<const Eigen::VectorXf>(w + lo.b1, ch);
            if (l == 0)
                A = (a == 0) ? P : Eigen::MatrixXf(headPrev.rightCols(T) + P);
            else
            {
                Eigen::MatrixXf An = A.rightCols(T) + P;
                A.swap(An);
            }
            if (wa)
            {
                auto& c2 = wa->layers[l];
                c2.X.swap(X);
                c2.U.swap(U);
                c2.P.swap(P);
                c2.s = s;
            }
            X.swap(Xn);
            Tin = T;
        }
        Eigen::MatrixXf Head;
        Head.noalias() = CMap(w + O.headW, L.headSize, ch) * A;
        if (L.headBias)
            Head.colwise() += Eigen::Map<const Eigen::VectorXf>(w + O.headB, L.headSize);
        if (wa)
        {
            wa->A = A;
            wa->Head = Head;
            wa->Xout = X;
        }
        Y.swap(X);
        headPrev.swap(Head);
    }
    const float hs = w[layout_.headScaleIndex];
    out.resize((size_t) headPrev.cols());
    for (Eigen::Index t = 0; t < headPrev.cols(); ++t)
        out[(size_t) t] = hs * headPrev(0, t);
}

void TrainableWaveNet::backward(const float* x, int lenX, const float* g, const std::vector<float>& dOut, Workspace& ws,
                                std::vector<float>& grad) const
{
    const int N = numKnobs_;
    Eigen::Map<const Eigen::RowVectorXf> xin(x, lenX);
    Eigen::Map<const Eigen::VectorXf> gv(g, N);
    const float* w = params_.data();
    float* gr = grad.data();
    const float hs = w[layout_.headScaleIndex];

    const auto& Llast = layout_.arrays.back();
    const int Tf = (int) ws.arrays.back().Head.cols();
    Eigen::MatrixXf dHead = Eigen::MatrixXf::Zero(Llast.headSize, Tf);
    for (int t = 0; t < Tf; ++t)
        dHead(0, t) = hs * dOut[(size_t) t];
    Eigen::MatrixXf dY; // grad wrt this array's output (from the next array's rechannel)

    for (int a = (int) layout_.arrays.size() - 1; a >= 0; --a)
    {
        const auto& L = layout_.arrays[(size_t) a];
        const auto& O = layout_.offsets[(size_t) a];
        const auto& wa = ws.arrays[(size_t) a];
        const int ch = L.channels, K = L.kernelSize, cs = L.conditionSize;

        MMap(gr + O.headW, L.headSize, ch).noalias() += dHead * wa.A.transpose();
        if (L.headBias)
            Eigen::Map<Eigen::VectorXf>(gr + O.headB, L.headSize) += dHead.rowwise().sum();
        Eigen::MatrixXf dA = CMap(w + O.headW, L.headSize, ch).transpose() * dHead;
        Eigen::MatrixXf dX = (a == (int) layout_.arrays.size() - 1) ? Eigen::MatrixXf::Zero(ch, wa.Xout.cols()) : dY;
        Eigen::MatrixXf dHeadPrev;

        for (int l = (int) L.dilations.size() - 1; l >= 0; --l)
        {
            const auto& lo = O.layers[(size_t) l];
            const auto& c = wa.layers[(size_t) l];
            const int d = L.dilations[(size_t) l];
            const int T = (int) c.P.cols();
            const int TinL = (int) c.X.cols();

            Eigen::MatrixXf dP = dA;
            dP.noalias() += CMap(w + lo.w1, ch, ch).transpose() * dX;
            MMap(gr + lo.w1, ch, ch).noalias() += dX * c.P.transpose();
            Eigen::Map<Eigen::VectorXf>(gr + lo.b1, ch) += dX.rowwise().sum();

            Eigen::MatrixXf dXin = Eigen::MatrixXf::Zero(ch, TinL);
            dXin.rightCols(T) = dX;
            Eigen::MatrixXf dAprev;
            if (l > 0)
            {
                dAprev = Eigen::MatrixXf::Zero(ch, TinL);
                dAprev.rightCols(T) = dA;
            }
            else if (a > 0)
            {
                dHeadPrev = Eigen::MatrixXf::Zero(ch, TinL);
                dHeadPrev.rightCols(T) = dA;
            }

            Eigen::MatrixXf dZ = (dP.array() * (1.0f - c.P.array().square())).matrix();
            // input_mixin audio column
            Eigen::VectorXf dMa = dZ * xin.tail(T).transpose();
            for (int i = 0; i < ch; ++i)
                gr[lo.mixin + (size_t) (i * cs)] += dMa[i];
            Eigen::MatrixXf dU;
            if (useKnobScale_ && N > 0)
            {
                Eigen::VectorXf zu = (dZ.array() * c.U.array()).rowwise().sum().matrix();
                MMap(gr + scaleOffsets_[(size_t) a][(size_t) l], ch, N).noalias() += zu * gv.transpose();
                dU = c.s.asDiagonal() * dZ;
            }
            else
                dU = std::move(dZ);
            Eigen::VectorXf r = dU.rowwise().sum();
            Eigen::Map<Eigen::VectorXf>(gr + lo.bias, ch) += r;
            if (N > 0)
                for (int i = 0; i < ch; ++i)
                    for (int k = 0; k < N; ++k)
                        gr[lo.mixin + (size_t) (i * cs + 1 + k)] += r[i] * gv[k];
            for (int k = 0; k < K; ++k)
            {
                convTap(gr + lo.conv, ch, K, k).noalias() += dU * c.X.middleCols(k * d, T).transpose();
                dXin.middleCols(k * d, T).noalias() += convTap(w + lo.conv, ch, K, k).transpose() * dU;
            }
            dX.swap(dXin);
            if (l > 0)
                dA.swap(dAprev);
        }
        // rechannel
        if (a == 0)
        {
            Eigen::VectorXf gw = dX * xin.transpose();
            for (int i = 0; i < ch; ++i)
                gr[O.rechannel + (size_t) (i * L.inputSize)] += gw[i];
        }
        else
        {
            MMap(gr + O.rechannel, ch, L.inputSize).noalias() += dX * wa.Y.transpose();
            dY = CMap(w + O.rechannel, ch, L.inputSize).transpose() * dX;
            dHead.swap(dHeadPrev);
        }
    }
}

nlohmann::json TrainableWaveNet::knobScaleJson() const
{
    nlohmann::json ks = nlohmann::json::array();
    for (size_t a = 0; a < layout_.arrays.size(); ++a)
    {
        nlohmann::json arr = nlohmann::json::array();
        const size_t n = (size_t) (layout_.arrays[a].channels * numKnobs_);
        for (size_t l = 0; l < layout_.arrays[a].dilations.size(); ++l)
        {
            const size_t off = scaleOffsets_[a][l];
            arr.push_back(std::vector<float>(params_.begin() + (long) off, params_.begin() + (long) (off + n)));
        }
        ks.push_back(arr);
    }
    return ks;
}
} // namespace pnam
