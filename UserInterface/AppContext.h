#pragma once
// Everything the pages share: the audio engine, the libraries and app settings.
#include <juce_data_structures/juce_data_structures.h>

#include "AppAudioEngine.h"
#include "Libraries.h"
#include "Presets.h"

namespace pnam::ui
{
struct AppContext
{
    AppContext()
        : dataDir(appDataDir()),
          models(dataDir / "Models", dataDir / "library.json"),
          captures(dataDir / "captures.json"),
          presets(dataDir / "Presets")
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ParametricNAMStudio";
        o.filenameSuffix = ".settings";
        o.folderName = "ParametricNAMStudio";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile>(o);
        std::filesystem::create_directories(dataDir / "Projects");
        try { models.load(); } catch (...) {}
        try { captures.load(); } catch (...) {}
    }
    juce::File projectsDir() const { return juce::File(juce::String(toUtf8(dataDir / "Projects"))); }

    std::filesystem::path dataDir;
    AppAudioEngine engine;
    ModelLibrary models;
    CaptureLibrary captures;
    PresetBank presets;
    std::unique_ptr<juce::PropertiesFile> settings;

    // pages listen to these to stay in sync
    juce::ChangeBroadcaster modelChanged, libraryChanged, presetsChanged;
};

inline juce::File toFile(const std::filesystem::path& p) { return juce::File(juce::String::fromUTF8(toUtf8(p).c_str())); }
inline std::filesystem::path toPath(const juce::File& f) { return fromUtf8(f.getFullPathName().toStdString()); }
} // namespace pnam::ui
