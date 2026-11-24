// pnam: command-line access to the Parametric NAM engine (the same code the app uses).
//
//   pnam info     FILE.nam
//   pnam render   MODEL.nam IN.wav OUT.wav [--knobs v1,v2,...]      knob values 0..1
//   pnam snapshot MODEL.nam OUT.nam --knobs v1,v2,...                 plain NAM at one setting
//   pnam plan     PROJECT.pnamproj
//   pnam train    PROJECT.pnamproj [--out MODEL.nam] [--steps N] [--arch standard|lite|feather|test] [--threads N]
//   pnam analyze  MODEL.nam [--knobs v1,...] [--against CAPTURE.nam]  harmonics, response, gain curve
//   pnam signal   OUT.wav [--seconds S] [--rate R]                    the built-in training signal
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "CapturePlanner.h"
#include "Libraries.h"
#include "NamFileInfo.h"
#include "ParametricModel.h"
#include "Snapshot.h"
#include "Trainer.h"
#include "TrainingSignal.h"
#include "Validation.h"
#include "WavIO.h"

namespace fs = std::filesystem;
static std::atomic<bool> gCancel { false };

static std::vector<float> parseList(const std::string& s)
{
    std::vector<float> v;
    std::stringstream ss(s);
    std::string t;
    while (std::getline(ss, t, ','))
        v.push_back(std::stof(t));
    return v;
}

struct Args
{
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;
    Args(int argc, char** argv)
    {
        for (int i = 2; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0 && i + 1 < argc)
                opt[a.substr(2)] = argv[++i];
            else
                pos.push_back(a);
        }
    }
    std::string get(const std::string& k, const std::string& d = "") const { auto it = opt.find(k); return it == opt.end() ? d : it->second; }
};

static int usage()
{
    std::puts("pnam info|render|snapshot|plan|train|analyze|signal ...  (see the header of Source/cli/pnam.cpp)");
    return 2;
}

static int cmdInfo(const Args& a)
{
    if (a.pos.empty()) return usage();
    auto i = pnam::readNamFileInfo(a.pos[0]);
    std::printf("name:          %s\narchitecture:  %s%s\nsample rate:   %.0f\nconfig:        %s\nweights:       %d (hash %s)\n",
                i.displayName().c_str(), i.architecture.c_str(), i.isParametric ? " (parametric)" : " (plain capture)", i.sampleRate,
                i.configSummary.c_str(), i.weightCount, pnam::hashToHex(i.weightsHash).c_str());
    if (!i.gearType.empty()) std::printf("gear type:     %s\n", i.gearType.c_str());
    if (i.inputLevelDbu) std::printf("input level:   %.2f dBu\n", *i.inputLevelDbu);
    if (i.loudness) std::printf("loudness:      %.2f dB\n", *i.loudness);
    for (const auto& k : i.knobs)
        std::printf("control:       %s (default %.2f)%s\n", k.name.c_str(), k.defaultValue, k.isSwitch() ? " [switch]" : "");
    for (const auto& w : i.warnings) std::printf("warning:       %s\n", w.c_str());
    return 0;
}

static std::unique_ptr<nam::DSP> loadAny(const fs::path& p, const std::vector<float>& knobs, int* nKnobs = nullptr)
{
    auto info = pnam::readNamFileInfo(p);
    if (info.isParametric)
    {
        auto m = pnam::loadPlayableModel(p);
        for (int k = 0; k < m.net->knobCount() && k < (int) knobs.size(); ++k)
            m.net->setKnob(k, knobs[(size_t) k]);
        if (nKnobs) *nKnobs = m.net->knobCount();
        return std::move(m.net);
    }
    return pnam::loadSourceCapture(p).dsp;
}

static int cmdRender(const Args& a)
{
    if (a.pos.size() < 3) return usage();
    auto dsp = loadAny(a.pos[0], parseList(a.get("knobs")));
    double sr = 0;
    auto x = pnam::readWavMono(a.pos[1], &sr);
    const double msr = dsp->GetExpectedSampleRate() > 0 ? dsp->GetExpectedSampleRate() : 48000;
    if (std::abs(sr - msr) > 0.5)
        std::fprintf(stderr, "warning: input is %.0f Hz, model is %.0f Hz (no resampling in the CLI)\n", sr, msr);
    pnam::writeWavMono(a.pos[2], pnam::renderOffline(*dsp, x), msr);
    return 0;
}

static int cmdSnapshot(const Args& a)
{
    if (a.pos.size() < 2) return usage();
    auto j = pnam::readJsonFile(a.pos[0]);
    pnam::writeJsonFile(a.pos[1], pnam::snapshotToPlain(j, parseList(a.get("knobs"))));
    return 0;
}

static void printPlan(const pnam::StudioProject& p)
{
    std::vector<std::vector<float>> caps;
    for (const auto& c : p.captures)
        if (c.assigned && !c.holdout)
            caps.push_back(c.knobs01);
    auto r = pnam::analyseCapturePlan(p.knobs, caps);
    const char* sev[] = { "info   ", "WARNING", "BLOCKER" };
    for (const auto& f : r.findings)
        std::printf("%s  %s\n", sev[(int) f.severity], f.text.c_str());
    if (!r.suggestions.empty())
        std::puts("\nSuggested next captures:");
    for (const auto& s : r.suggestions)
        std::printf("  - %s   (%s)\n", pnam::describeSetting(p.knobs, s.knobs01).c_str(), s.reason.c_str());
    std::printf("\n%d captures, %d recommended, %s\n", r.captured, r.recommendedTotal, r.trainable ? "ready to train" : "NOT ready to train");
}

static int cmdPlan(const Args& a)
{
    if (a.pos.empty()) return usage();
    printPlan(pnam::StudioProject::load(a.pos[0]));
    return 0;
}

static int cmdTrain(const Args& a)
{
    if (a.pos.empty()) return usage();
    auto p = pnam::StudioProject::load(a.pos[0]);
    pnam::TrainingConfig cfg;
    cfg.knobs = p.knobs;
    for (const auto& c : p.captures)
    {
        if (!c.assigned) continue;
        pnam::CaptureSpec s;
        s.path = c.path;
        s.knobs01 = c.knobs01;
        s.holdout = c.holdout;
        s.label = c.label.empty() ? c.path.stem().string() : c.label;
        cfg.captures.push_back(s);
    }
    cfg.architecture = a.get("arch", p.architecture);
    cfg.steps = std::stoi(a.get("steps", std::to_string(p.steps)));
    cfg.threads = std::stoi(a.get("threads", "0"));
    cfg.knobScale = p.knobScale;
    cfg.signal.seconds = p.signalSeconds;
    cfg.signal.userDiFiles = p.diFiles;
    cfg.metadata = { { "name", p.ampName.empty() ? "Parametric model" : p.ampName }, { "gear_make", p.manufacturer },
                     { "gear_model", p.ampName }, { "modeled_by", p.creator } };
    std::signal(SIGINT, [](int) { gCancel = true; });
    auto res = pnam::trainParametric(cfg, [](const pnam::TrainingProgress& pr) {
        if (pr.stage == "training" && pr.valEsr >= 0)
            std::printf("step %5d/%d  loss %.4f  val ESR %.4f  held-out ESR %.4f\n", pr.step, pr.totalSteps, pr.loss, pr.valEsr, pr.holdoutEsr);
        else if (pr.stage != "training")
            std::printf("[%s] %s\n", pr.stage.c_str(), pr.message.c_str());
        std::fflush(stdout);
    }, &gCancel);
    const fs::path out = a.get("out", (fs::path(a.pos[0]).parent_path() / (p.ampName.empty() ? "model" : p.ampName)).string() + ".nam");
    pnam::writeJsonFile(out, res.model);
    for (const auto& c : res.report.captures)
        std::printf("  %-28s %s ESR %.4f %s%s\n", c.label.c_str(), c.holdout ? "[held-out]" : "[trained] ", c.esr,
                    pnam::esrVerdict(c.esr).c_str(),
                    c.holdout && c.nearestEsr >= 0 ? (" | nearest capture " + c.nearestLabel + " ESR " + std::to_string(c.nearestEsr)).c_str() : "");
    for (const auto& w : res.report.warnings) std::printf("warning: %s\n", w.c_str());
    std::printf("%s\nwrote %s\n", res.report.verdict.c_str(), out.string().c_str());
    return 0;
}

static int cmdAnalyze(const Args& a)
{
    if (a.pos.empty()) return usage();
    const auto knobs = parseList(a.get("knobs"));
    auto dsp = loadAny(a.pos[0], knobs);
    const double sr = dsp->GetExpectedSampleRate() > 0 ? dsp->GetExpectedSampleRate() : 48000;
    std::unique_ptr<nam::DSP> other;
    if (!a.get("against").empty())
        other = loadAny(a.get("against"), knobs);
    auto analyse = [&](nam::DSP& d, const char* title) {
        pnam::RenderFn fn = [&](const std::vector<float>& x) { return pnam::renderOffline(d, x); };
        std::printf("== %s\nharmonics of 220 Hz:\n", title);
        for (double lvl : { -30.0, -18.0, -6.0 })
        {
            auto h = pnam::measureHarmonics(fn, sr, 220.0, lvl, 6);
            std::printf("  in %5.0f dBFS: out %6.1f dBFS, THD %6.2f%%  H2..H6:", lvl, h.fundamentalDb, h.thdPercent);
            for (double v : h.harmonicsDb) std::printf(" %6.1f", v);
            std::puts(" dB");
        }
        std::puts("response at -30 dBFS (dB):");
        for (const auto& p : pnam::measureResponse(fn, sr, -30.0, { 63, 125, 250, 500, 1000, 2000, 4000, 8000 }))
            std::printf("  %6.0f Hz %6.1f\n", p.freq, p.gainDb);
        std::puts("gain curve, 1 kHz (in -> out dBFS):");
        for (const auto& [i, o] : pnam::measureGainCurve(fn, sr, { -48, -36, -24, -12, -6, 0 }))
            std::printf("  %5.0f -> %6.1f\n", i, o);
    };
    analyse(*dsp, a.pos[0].c_str());
    if (other) analyse(*other, a.get("against").c_str());
    return 0;
}

static int cmdSignal(const Args& a)
{
    if (a.pos.empty()) return usage();
    pnam::TrainingSignalOptions o;
    o.seconds = std::stod(a.get("seconds", "60"));
    o.sampleRate = std::stod(a.get("rate", "48000"));
    pnam::writeWavMono(a.pos[0], pnam::makeTrainingSignal(o), o.sampleRate);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) return usage();
    const std::string c = argv[1];
    Args a(argc, argv);
    try
    {
        if (c == "info") return cmdInfo(a);
        if (c == "render") return cmdRender(a);
        if (c == "snapshot") return cmdSnapshot(a);
        if (c == "plan") return cmdPlan(a);
        if (c == "train") return cmdTrain(a);
        if (c == "analyze") return cmdAnalyze(a);
        if (c == "signal") return cmdSignal(a);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return usage();
}
