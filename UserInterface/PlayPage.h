#pragma once
// The playing page: the loaded Parametric NAM model as an amp head whose controls are exactly
// the controls in the model file, input/output stages, and the dual-IR cabinet section.
#include "AppContext.h"
#include "Widgets.h"

namespace pnam::ui
{
class AmpFace : public juce::Component
{
public:
    explicit AmpFace(AppContext& ctx);
    void rebuild();                // call when the model changes
    void syncFromEngine();         // pull knob positions (after preset recall)
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx_;
    juce::OwnedArray<LabelledKnob> knobs_;
    juce::OwnedArray<juce::TextButton> switches_;
    juce::OwnedArray<juce::Label> switchCaptions_;
    std::vector<int> knobIndexForControl_; // per drawn control: model knob index
    std::vector<bool> isSwitch_;
    juce::String title_, subtitle_, badge_;
    juce::Colour badgeColour_;
};

class IRStrip : public juce::Component
{
public:
    IRStrip(AppContext& ctx, int slot);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void push();
    AppContext& ctx_;
    int slot_;
    juce::ToggleButton enable_ { "On" }, invert_ { "Phase" };
    juce::TextButton load_ { "Load IR..." }, clear_ { "Clear" };
    juce::Label name_, details_;
    LabelledKnob level_ { "Level", false }, pan_ { "Pan", false };
    std::unique_ptr<juce::FileChooser> chooser_;
};

class PlayPage : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PlayPage(AppContext& ctx);
    ~PlayPage() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void syncFromEngine();

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    AppContext& ctx_;
    AmpFace face_;
    Meter inMeter_, outMeter_;
    LabelledKnob inGain_ { "Input", false }, outGain_ { "Output", false }, gateThresh_ { "Gate", false };
    juce::ToggleButton gateOn_ { "Noise gate" }, normalize_ { "Normalize level" }, cabOn_ { "Cabinet" }, stereo_ { "Stereo" };
    IRStrip irA_, irB_;
    juce::TextButton abButton_ { "A: Parametric model" };
    juce::Label abLabel_, cabWarning_, empty_;
};
} // namespace pnam::ui
