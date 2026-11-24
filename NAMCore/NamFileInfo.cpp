#include "NamFileInfo.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pnam
{
uint64_t fnv1a(const void* data, size_t bytes, uint64_t h)
{
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::string hashToHex(uint64_t h)
{
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long) h);
    return b;
}

bool isKnownPlainArchitecture(const std::string& a)
{
    static const char* known[] = { "WaveNet", "LSTM", "ConvNet", "Linear", "SlimmableContainer", "SlimmableWavenet",
                                   "A2", "Sequential", "Container" };
    return std::any_of(std::begin(known), std::end(known), [&](const char* k) { return a == k; });
}

std::string NamFileInfo::displayName() const
{
    if (!name.empty())
        return name;
    if (!gearMake.empty() || !gearModel.empty())
        return (gearMake + " " + gearModel);
    return path.stem().string();
}

namespace
{
std::string str(const nlohmann::json& j, const char* k)
{
    if (j.is_object() && j.contains(k) && j[k].is_string())
        return j[k].get<std::string>();
    return {};
}
std::optional<double> num(const nlohmann::json& j, const char* k)
{
    if (j.is_object() && j.contains(k) && j[k].is_number())
        return j[k].get<double>();
    return std::nullopt;
}
} // namespace

NamFileInfo namFileInfoFromJson(const nlohmann::json& j, const std::filesystem::path& path)
{
    if (!j.is_object() || !j.contains("architecture") || !j.contains("config") || !j.contains("weights"))
        throw std::runtime_error("not a NAM model file (needs architecture, config and weights)");

    NamFileInfo info;
    info.path = path;
    info.fileName = path.filename().string();
    info.version = str(j, "version");
    info.architecture = str(j, "architecture");
    info.sampleRate = num(j, "sample_rate").value_or(-1.0);
    if (info.sampleRate <= 0)
        info.warnings.push_back("No sample rate in file: NAM assumes 48 kHz.");

    const auto& w = j["weights"];
    if (w.is_array())
    {
        info.weightCount = (int) w.size();
        std::vector<float> wf;
        wf.reserve(w.size());
        for (const auto& v : w)
            wf.push_back(v.is_number() ? v.get<float>() : 0.0f);
        info.weightsHash = fnv1a(wf.data(), wf.size() * sizeof(float));
    }

    if (j.contains("metadata") && j["metadata"].is_object())
    {
        const auto& m = j["metadata"];
        info.name = str(m, "name");
        info.modeledBy = str(m, "modeled_by");
        info.gearMake = str(m, "gear_make");
        info.gearModel = str(m, "gear_model");
        info.gearType = str(m, "gear_type");
        info.toneType = str(m, "tone_type");
        info.loudness = num(m, "loudness");
        info.inputLevelDbu = num(m, "input_level_dbu");
        info.outputLevelDbu = num(m, "output_level_dbu");
        if (m.contains("date") && m["date"].is_object())
        {
            const auto& d = m["date"];
            char b[32];
            std::snprintf(b, sizeof b, "%04d-%02d-%02d", d.value("year", 0), d.value("month", 0), d.value("day", 0));
            info.date = b;
        }
        if (m.contains("training") && m["training"].is_object())
        {
            const auto& t = m["training"];
            if (t.contains("validation_esr") && t["validation_esr"].is_number())
                info.validationEsr = t["validation_esr"].get<double>();
        }
        if (!info.gearType.empty())
            info.cabFromMetadata = info.gearType.find("cab") != std::string::npos ? NamFileInfo::Cab::yes : NamFileInfo::Cab::no;
    }

    const auto& cfg = j["config"];
    info.isParametric = info.architecture == "ParametricWaveNet" || info.architecture == "CoroParametricWaveNet";
    info.hasKnobScale = j.contains("knob_scale");
    if (info.isParametric && cfg.contains("knobs") && cfg["knobs"].is_array())
    {
        for (const auto& k : cfg["knobs"])
        {
            KnobInfo ki;
            ki.name = str(k, "name");
            ki.defaultValue = (float) num(k, "default").value_or(0.5);
            if (k.contains("labels") && k["labels"].is_array())
                for (const auto& l : k["labels"])
                    if (l.is_string())
                        ki.labels.push_back(l.get<std::string>());
            if (k.contains("range") && k["range"].is_array() && k["range"].size() == 2)
            {
                ki.displayMin = k["range"][0].get<float>();
                ki.displayMax = k["range"][1].get<float>();
            }
            info.knobs.push_back(ki);
        }
    }

    if (cfg.is_object() && cfg.contains("layers") && cfg["layers"].is_array())
    {
        int rf = 1;
        std::ostringstream chs;
        for (size_t a = 0; a < cfg["layers"].size(); ++a)
        {
            const auto& L = cfg["layers"][a];
            const int K = L.value("kernel_size", 1);
            int dsum = 0;
            if (L.contains("dilations"))
                for (const auto& d : L["dilations"])
                    dsum += d.get<int>();
            rf += (K - 1) * dsum;
            chs << (a ? "/" : "") << L.value("channels", 0);
        }
        info.receptiveField = rf;
        info.configSummary = info.architecture + ", " + std::to_string(cfg["layers"].size()) + " layer arrays, " + chs.str()
                             + " channels, receptive field " + std::to_string(rf) + " samples";
    }
    else if (info.architecture == "LSTM" && cfg.is_object())
        info.configSummary = "LSTM, " + std::to_string(cfg.value("num_layers", 0)) + " layers, hidden "
                             + std::to_string(cfg.value("hidden_size", 0));
    else
        info.configSummary = info.architecture;

    if (!info.isParametric && !isKnownPlainArchitecture(info.architecture))
        info.warnings.push_back("Architecture \"" + info.architecture + "\" is not one NAM core is known to run.");
    if (info.isParametric && info.knobs.empty())
        info.warnings.push_back("Parametric file without config.knobs.");
    return info;
}

NamFileInfo readNamFileInfo(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + path.string());
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(text);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(path.filename().string() + ": not valid JSON (" + e.what() + ")");
    }
    auto info = namFileInfoFromJson(j, path);
    info.fileHash = fnv1a(text.data(), text.size());
    return info;
}
} // namespace pnam
