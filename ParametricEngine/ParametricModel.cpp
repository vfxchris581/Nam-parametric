#include "ParametricModel.h"

#include <fstream>
#include <stdexcept>

#include <NAM/get_dsp.h>

namespace pnam
{
nlohmann::json readJsonFile(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + path.string());
    try
    {
        return nlohmann::json::parse(f);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(path.filename().string() + ": not valid JSON (" + e.what() + ")");
    }
}

void writeJsonFile(const std::filesystem::path& path, const nlohmann::json& j, int indent)
{
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot write " + path.string());
        f << j.dump(indent);
        if (!f)
            throw std::runtime_error("write failed: " + path.string());
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path);
    }
}

PlayableModel playableModelFromJson(const nlohmann::json& j, const std::filesystem::path& path)
{
    PlayableModel m;
    m.info = namFileInfoFromJson(j, path);
    if (!ParametricWaveNet::isParametric(j))
        throw std::runtime_error(m.info.displayName()
                                 + " is a plain NAM capture (architecture \"" + m.info.architecture
                                 + "\"). It can be used as a source capture in the Capture Studio, "
                                   "but it is not a parametric model.");
    m.net = ParametricWaveNet::fromJson(j);
    m.json = j;
    return m;
}

PlayableModel loadPlayableModel(const std::filesystem::path& path)
{
    auto j = readJsonFile(path);
    auto m = playableModelFromJson(j, path);
    m.info.fileHash = readNamFileInfo(path).fileHash;
    return m;
}

SourceCapture loadSourceCapture(const std::filesystem::path& path)
{
    SourceCapture c;
    c.info = readNamFileInfo(path);
    if (c.info.isParametric)
        throw std::runtime_error(c.info.fileName + " is already a parametric model, not a single capture.");
    try
    {
        c.dsp = nam::get_dsp(path);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(c.info.fileName + ": NAM core cannot load it (" + e.what() + ")");
    }
    if (!c.dsp)
        throw std::runtime_error(c.info.fileName + ": NAM core returned no model");
    return c;
}

std::vector<float> renderOffline(nam::DSP& dsp, const std::vector<float>& input, int blockSize)
{
    const double sr = dsp.GetExpectedSampleRate() > 0 ? dsp.GetExpectedSampleRate() : 48000.0;
    dsp.ResetAndPrewarm(sr, blockSize);
    std::vector<float> out(input.size());
    std::vector<NAM_SAMPLE> inBuf((size_t) blockSize), outBuf((size_t) blockSize);
    for (size_t p = 0; p < input.size(); p += (size_t) blockSize)
    {
        const int n = (int) std::min<size_t>((size_t) blockSize, input.size() - p);
        for (int i = 0; i < n; ++i)
            inBuf[(size_t) i] = (NAM_SAMPLE) input[p + (size_t) i];
        NAM_SAMPLE* ip = inBuf.data();
        NAM_SAMPLE* op = outBuf.data();
        dsp.process(&ip, &op, n);
        for (int i = 0; i < n; ++i)
            out[p + (size_t) i] = (float) outBuf[(size_t) i];
    }
    return out;
}
} // namespace pnam
