#include "Libraries.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <set>
#include <stdexcept>

#include "ParametricModel.h"
#include "Validation.h"

namespace pnam
{
std::string toUtf8(const fs::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

fs::path appDataDir()
{
    fs::path base;
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA"))
        base = a;
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME"))
        base = fs::path(h) / "Library" / "Application Support";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"))
        base = x;
    else if (const char* h = std::getenv("HOME"))
        base = fs::path(h) / ".local" / "share";
#endif
    if (base.empty())
        base = fs::temp_directory_path();
    auto d = base / "ParametricNAMStudio";
    fs::create_directories(d);
    return d;
}

std::string todayString()
{
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm {};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", &tm);
    return b;
}

static std::string cabToString(CaptureEntry::Cab c)
{
    switch (c)
    {
        case CaptureEntry::Cab::no: return "no";
        case CaptureEntry::Cab::yes: return "yes";
        case CaptureEntry::Cab::likely: return "likely";
        default: return "unknown";
    }
}
static CaptureEntry::Cab cabFromString(const std::string& s)
{
    if (s == "no") return CaptureEntry::Cab::no;
    if (s == "yes") return CaptureEntry::Cab::yes;
    if (s == "likely") return CaptureEntry::Cab::likely;
    return CaptureEntry::Cab::unknown;
}

// ============================================================================ CaptureLibrary
CaptureLibrary::CaptureLibrary(fs::path indexFile) : index_(std::move(indexFile)) {}

std::string CaptureLibrary::ampFromMetadata(const NamFileInfo& info)
{
    std::string s = info.gearMake;
    if (!info.gearModel.empty())
        s += (s.empty() ? "" : " ") + info.gearModel;
    return s;
}

CaptureEntry::Cab CaptureLibrary::measureCab(const fs::path& p, std::string* reason)
{
    try
    {
        auto cap = loadSourceCapture(p);
        const double sr = cap.dsp->GetExpectedSampleRate() > 0 ? cap.dsp->GetExpectedSampleRate() : 48000.0;
        RenderFn fn = [&](const std::vector<float>& x) { return renderOffline(*cap.dsp, x); };
        auto r = measureResponse(fn, sr, -30.0, { 1000.0, 2000.0, 7000.0, 10000.0 });
        const double mid = 0.5 * (r[0].gainDb + r[1].gainDb), high = 0.5 * (r[2].gainDb + r[3].gainDb);
        const double drop = mid - high;
        char b[128];
        std::snprintf(b, sizeof b, "7-10 kHz is %.0f dB below 1-2 kHz", drop);
        if (reason)
            *reason = b;
        // A loudspeaker rolls off steeply above ~5 kHz; an amp into a load box does not.
        return drop > 24.0 ? CaptureEntry::Cab::likely : CaptureEntry::Cab::no;
    }
    catch (const std::exception& e)
    {
        if (reason)
            *reason = e.what();
        return CaptureEntry::Cab::unknown;
    }
}

void CaptureLibrary::load()
{
    entries_.clear();
    if (!fs::exists(index_))
        return;
    auto j = readJsonFile(index_);
    for (const auto& e : j.value("captures", nlohmann::json::array()))
    {
        CaptureEntry c;
        c.path = fromUtf8(e.value("path", std::string()));
        if (!fs::exists(c.path))
            continue;
        try
        {
            c.info = readNamFileInfo(c.path);
        }
        catch (...)
        {
            continue;
        }
        c.amp = e.value("amp", std::string());
        c.ampFromMetadata = e.value("amp_from_metadata", false);
        c.cab = cabFromString(e.value("cab", std::string("unknown")));
        c.cabReason = e.value("cab_reason", std::string());
        entries_.push_back(std::move(c));
    }
}

void CaptureLibrary::save() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : entries_)
        arr.push_back({ { "path", toUtf8(c.path) }, { "amp", c.amp }, { "amp_from_metadata", c.ampFromMetadata },
                        { "cab", cabToString(c.cab) }, { "cab_reason", c.cabReason } });
    fs::create_directories(index_.parent_path());
    writeJsonFile(index_, { { "version", 1 }, { "captures", arr } }, 2);
}

ImportResult CaptureLibrary::importPaths(const std::vector<fs::path>& in)
{
    ImportResult r;
    std::vector<fs::path> files;
    for (const auto& p : in)
    {
        std::error_code ec;
        if (fs::is_directory(p, ec))
        {
            for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
                if (!ec && it->is_regular_file(ec) && it->path().extension() == ".nam")
                    files.push_back(it->path());
        }
        else if (p.extension() == ".nam")
            files.push_back(p);
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files)
    {
        NamFileInfo info;
        try
        {
            info = readNamFileInfo(f);
        }
        catch (const std::exception& e)
        {
            r.failed.push_back({ f, e.what() });
            continue;
        }
        if (info.isParametric)
        {
            r.parametricSkipped.push_back(f);
            continue;
        }
        const bool dup = std::any_of(entries_.begin(), entries_.end(), [&](const CaptureEntry& e) {
            return e.path == f || (e.info.weightsHash == info.weightsHash && e.info.weightCount == info.weightCount);
        });
        if (dup)
        {
            r.duplicates.push_back(f);
            continue;
        }
        CaptureEntry c;
        c.path = f;
        c.info = info;
        c.amp = ampFromMetadata(info);
        c.ampFromMetadata = !c.amp.empty();
        if (info.cabFromMetadata == NamFileInfo::Cab::yes)
            c.cab = CaptureEntry::Cab::yes, c.cabReason = "metadata gear_type \"" + info.gearType + "\"";
        else if (info.cabFromMetadata == NamFileInfo::Cab::no)
            c.cab = CaptureEntry::Cab::no, c.cabReason = "metadata gear_type \"" + info.gearType + "\"";
        else
            c.cab = measureCab(f, &c.cabReason);
        entries_.push_back(std::move(c));
        r.added.push_back(f);
    }
    return r;
}

CaptureEntry* CaptureLibrary::find(const fs::path& p)
{
    for (auto& e : entries_)
        if (e.path == p)
            return &e;
    return nullptr;
}

void CaptureLibrary::setAmp(const fs::path& p, const std::string& amp)
{
    if (auto* e = find(p))
    {
        e->amp = amp;
        e->ampFromMetadata = false;
    }
}

void CaptureLibrary::remove(const fs::path& p)
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const CaptureEntry& e) { return e.path == p; }), entries_.end());
}

std::vector<std::string> CaptureLibrary::ampNames() const
{
    std::set<std::string> s;
    for (const auto& e : entries_)
        if (!e.amp.empty())
            s.insert(e.amp);
    return { s.begin(), s.end() };
}

std::vector<const CaptureEntry*> CaptureLibrary::capturesOfAmp(const std::string& amp) const
{
    std::vector<const CaptureEntry*> v;
    for (const auto& e : entries_)
        if (e.amp == amp)
            v.push_back(&e);
    return v;
}

std::vector<std::string> CaptureLibrary::groupProblems(const std::vector<const CaptureEntry*>& g)
{
    std::vector<std::string> p;
    std::set<int> rates;
    std::set<long> levels;
    int cab = 0, nocab = 0;
    for (const auto* e : g)
    {
        rates.insert((int) (e->info.sampleRate > 0 ? e->info.sampleRate : 48000));
        if (e->info.inputLevelDbu)
            levels.insert(std::lround(*e->info.inputLevelDbu * 10));
        if (e->cab == CaptureEntry::Cab::yes || e->cab == CaptureEntry::Cab::likely)
            ++cab;
        else if (e->cab == CaptureEntry::Cab::no)
            ++nocab;
    }
    if (rates.size() > 1)
        p.push_back("Captures use different sample rates; train captures of one sample rate together.");
    if (levels.size() > 1)
        p.push_back("Captures were calibrated at different input levels (input_level_dbu differs).");
    if (cab && nocab)
        p.push_back("Some captures include a cabinet and some do not: they are probably not the same rig.");
    return p;
}

// ============================================================================ ModelLibrary
nlohmann::json ModelEntry::toJson() const
{
    return { { "path", toUtf8(path) }, { "name", name }, { "manufacturer", manufacturer }, { "amp", amp },
             { "channel", channel }, { "description", description }, { "creator", creator }, { "cabinet", cabinet },
             { "category", category }, { "tags", tags }, { "collections", collections }, { "favorite", favorite },
             { "validation", validation } };
}

ModelEntry ModelEntry::fromJson(const nlohmann::json& j)
{
    ModelEntry e;
    e.path = fromUtf8(j.value("path", std::string()));
    e.name = j.value("name", std::string());
    e.manufacturer = j.value("manufacturer", std::string());
    e.amp = j.value("amp", std::string());
    e.channel = j.value("channel", std::string());
    e.description = j.value("description", std::string());
    e.creator = j.value("creator", std::string());
    e.cabinet = j.value("cabinet", std::string());
    e.category = j.value("category", std::string());
    e.tags = j.value("tags", std::vector<std::string>());
    e.collections = j.value("collections", std::vector<std::string>());
    e.favorite = j.value("favorite", false);
    e.validation = j.value("validation", std::string());
    return e;
}

const std::vector<std::string>& ModelLibrary::standardCategories()
{
    static const std::vector<std::string> c = { "Clean", "Crunch", "Overdrive", "High Gain", "Metal", "Vintage", "Boutique" };
    return c;
}

ModelLibrary::ModelLibrary(fs::path modelsDir, fs::path indexFile) : dir_(std::move(modelsDir)), index_(std::move(indexFile))
{
    fs::create_directories(dir_);
}

static void fillFromInfo(ModelEntry& e)
{
    if (e.name.empty())
        e.name = e.info.displayName();
    if (e.manufacturer.empty())
        e.manufacturer = e.info.gearMake;
    if (e.amp.empty())
        e.amp = e.info.gearModel;
    if (e.creator.empty())
        e.creator = e.info.modeledBy;
    if (e.category.empty() && !e.info.toneType.empty())
    {
        const auto& t = e.info.toneType;
        e.category = t == "clean" ? "Clean" : t == "crunch" ? "Crunch" : t == "hi_gain" ? "High Gain" : t == "overdrive" ? "Overdrive" : "";
    }
}

void ModelLibrary::load()
{
    entries_.clear();
    if (fs::exists(index_))
    {
        auto j = readJsonFile(index_);
        for (const auto& m : j.value("models", nlohmann::json::array()))
        {
            auto e = ModelEntry::fromJson(m);
            if (!fs::exists(e.path))
                continue;
            try
            {
                e.info = readNamFileInfo(e.path);
            }
            catch (...)
            {
                continue;
            }
            if (!e.info.isParametric)
                continue;
            fillFromInfo(e);
            entries_.push_back(std::move(e));
        }
    }
    rescan();
}

void ModelLibrary::rescan()
{
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir_, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
    {
        if (it->path().extension() != ".nam" || find(it->path()))
            continue;
        try
        {
            ModelEntry e;
            e.path = it->path();
            e.info = readNamFileInfo(e.path);
            if (!e.info.isParametric)
                continue;
            fillFromInfo(e);
            entries_.push_back(std::move(e));
        }
        catch (...)
        {
        }
    }
}

void ModelLibrary::save() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries_)
        arr.push_back(e.toJson());
    fs::create_directories(index_.parent_path());
    writeJsonFile(index_, { { "version", 1 }, { "models", arr } }, 2);
}

ModelEntry& ModelLibrary::addModel(const fs::path& file, bool copy)
{
    auto info = readNamFileInfo(file);
    if (!info.isParametric)
        throw std::runtime_error(file.filename().string()
                                 + " is a plain NAM capture. Plain captures are training material: import them in the "
                                   "Capture Studio and train a parametric model.");
    ParametricWaveNet::fromJson(readJsonFile(file)); // must actually load
    fs::path dest = file;
    if (copy && file.parent_path() != dir_)
    {
        dest = dir_ / file.filename();
        for (int i = 2; fs::exists(dest); ++i)
            dest = dir_ / (file.stem().string() + " (" + std::to_string(i) + ").nam");
        fs::copy_file(file, dest);
    }
    if (auto* existing = find(dest))
        return *existing;
    ModelEntry e;
    e.path = dest;
    e.info = readNamFileInfo(dest);
    fillFromInfo(e);
    entries_.push_back(std::move(e));
    return entries_.back();
}

ModelEntry& ModelLibrary::addModelJson(const nlohmann::json& model, const std::string& stem)
{
    fs::path dest = dir_ / (stem + ".nam");
    for (int i = 2; fs::exists(dest); ++i)
        dest = dir_ / (stem + " (" + std::to_string(i) + ").nam");
    writeJsonFile(dest, model);
    return addModel(dest, false);
}

ModelEntry* ModelLibrary::find(const fs::path& p)
{
    for (auto& e : entries_)
        if (e.path == p)
            return &e;
    return nullptr;
}

void ModelLibrary::remove(const fs::path& p, bool deleteFile)
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const ModelEntry& e) { return e.path == p; }), entries_.end());
    if (deleteFile && p.parent_path() == dir_)
    {
        std::error_code ec;
        fs::remove(p, ec);
    }
}

// ============================================================================ StudioProject
nlohmann::json StudioProject::toJson() const
{
    nlohmann::json kn = nlohmann::json::array();
    for (const auto& k : knobs)
        kn.push_back({ { "name", k.name }, { "default", k.defaultValue }, { "labels", k.labels }, { "range", { k.displayMin, k.displayMax } } });
    nlohmann::json caps = nlohmann::json::array();
    for (const auto& c : captures)
        caps.push_back({ { "path", toUtf8(c.path) }, { "label", c.label }, { "knobs", c.knobs01 }, { "holdout", c.holdout }, { "assigned", c.assigned } });
    nlohmann::json vers = nlohmann::json::array();
    for (const auto& v : versions)
        vers.push_back({ { "number", v.number }, { "file", toUtf8(v.file) }, { "date", v.date }, { "architecture", v.architecture },
                         { "verdict", v.verdict }, { "trained_esr", v.trainedEsr }, { "holdout_esr", v.holdoutEsr }, { "baseline_esr", v.baselineEsr } });
    std::vector<std::string> di;
    for (const auto& d : diFiles)
        di.push_back(toUtf8(d));
    return { { "format", "pnam-studio-project" }, { "version", 1 }, { "amp", ampName }, { "manufacturer", manufacturer },
             { "channel", channel }, { "description", description }, { "creator", creator }, { "category", category },
             { "knobs", kn }, { "captures", caps }, { "architecture", architecture }, { "steps", steps }, { "knob_scale", knobScale },
             { "signal_seconds", signalSeconds }, { "di_files", di }, { "versions", vers } };
}

StudioProject StudioProject::fromJson(const nlohmann::json& j, const fs::path& file)
{
    StudioProject p;
    p.file = file;
    p.ampName = j.value("amp", std::string());
    p.manufacturer = j.value("manufacturer", std::string());
    p.channel = j.value("channel", std::string());
    p.description = j.value("description", std::string());
    p.creator = j.value("creator", std::string());
    p.category = j.value("category", std::string());
    for (const auto& k : j.value("knobs", nlohmann::json::array()))
    {
        KnobInfo ki;
        ki.name = k.value("name", std::string());
        ki.defaultValue = k.value("default", 0.5f);
        ki.labels = k.value("labels", std::vector<std::string>());
        if (k.contains("range") && k["range"].size() == 2)
            ki.displayMin = k["range"][0].get<float>(), ki.displayMax = k["range"][1].get<float>();
        p.knobs.push_back(ki);
    }
    for (const auto& c : j.value("captures", nlohmann::json::array()))
    {
        ProjectCapture pc;
        pc.path = fromUtf8(c.value("path", std::string()));
        pc.label = c.value("label", std::string());
        pc.knobs01 = c.value("knobs", std::vector<float>());
        pc.holdout = c.value("holdout", false);
        pc.assigned = c.value("assigned", false);
        p.captures.push_back(pc);
    }
    p.architecture = j.value("architecture", std::string("standard"));
    p.steps = j.value("steps", 3000);
    p.knobScale = j.value("knob_scale", true);
    p.signalSeconds = j.value("signal_seconds", 90.0);
    for (const auto& d : j.value("di_files", std::vector<std::string>()))
        p.diFiles.push_back(fromUtf8(d));
    for (const auto& v : j.value("versions", nlohmann::json::array()))
    {
        ModelVersion mv;
        mv.number = v.value("number", 0);
        mv.file = fromUtf8(v.value("file", std::string()));
        mv.date = v.value("date", std::string());
        mv.architecture = v.value("architecture", std::string());
        mv.verdict = v.value("verdict", std::string());
        mv.trainedEsr = v.value("trained_esr", -1.0);
        mv.holdoutEsr = v.value("holdout_esr", -1.0);
        mv.baselineEsr = v.value("baseline_esr", -1.0);
        p.versions.push_back(mv);
    }
    return p;
}

void StudioProject::save() const
{
    if (file.empty())
        throw std::runtime_error("project has no file");
    fs::create_directories(file.parent_path());
    writeJsonFile(file, toJson(), 2);
}

StudioProject StudioProject::load(const fs::path& f)
{
    auto j = readJsonFile(f);
    if (j.value("format", std::string()) != "pnam-studio-project")
        throw std::runtime_error(f.filename().string() + " is not a Parametric NAM Studio project");
    return fromJson(j, f);
}
} // namespace pnam
