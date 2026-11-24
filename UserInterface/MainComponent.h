#pragma once
#include "AppContext.h"
#include "LibraryPage.h"
#include "PlayPage.h"
#include "StudioPage.h"
#include "StudioLook.h"

namespace pnam::ui
{
class SettingsPage : public juce::Component
{
public:
    explicit SettingsPage(AppContext& ctx);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx_;
    juce::AudioDeviceSelectorComponent selector_;
    juce::ComboBox inputChannel_;
    juce::Label inputLabel_, about_;
    juce::TextButton openData_ { "Open data folder" };
};

class HeaderBar : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit HeaderBar(AppContext& ctx, std::function<void()> onPresetLoaded);
    ~HeaderBar() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refreshPresets();
    void setCurrentPreset(const juce::File& f);

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refreshPresets(); }
    void presetMenu();
    void savePresetAs(const juce::String& suggested);
    AppContext& ctx_;
    std::function<void()> onPresetLoaded_;
    juce::ComboBox presets_;
    juce::TextButton presetActions_ { "Preset..." }, save_ { "Save" };
    juce::String status_;
    juce::File current_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    AppContext& context() { return ctx_; }
    juce::TabbedComponent& tabs() { return tabs_; }
    PlayPage& playPage() { return *play_; }
    StudioPage& studioPage() { return *studio_; }
    LibraryPage& libraryPage() { return *library_; }
    HeaderBar& header() { return *header_; }

private:
    StudioLook look_;
    AppContext ctx_;
    std::unique_ptr<HeaderBar> header_;
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<PlayPage> play_;
    std::unique_ptr<LibraryPage> library_;
    std::unique_ptr<StudioPage> studio_;
    std::unique_ptr<SettingsPage> settings_;
};
} // namespace pnam::ui
