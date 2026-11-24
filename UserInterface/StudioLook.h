#pragma once
// Visual identity: a dark studio look with warm amber "tube glow" accents. Original artwork only.
#include <juce_gui_basics/juce_gui_basics.h>

namespace pnam::ui
{
namespace colours
{
const juce::Colour background { 0xff121316 };
const juce::Colour panel { 0xff1b1d22 };
const juce::Colour panelRaised { 0xff23262d };
const juce::Colour outline { 0xff2f333b };
const juce::Colour text { 0xffe6e2da };
const juce::Colour textDim { 0xff8f949c };
const juce::Colour accent { 0xffe8a33d };     // amber
const juce::Colour accentDim { 0xff8a6327 };
const juce::Colour good { 0xff49b89c };
const juce::Colour warn { 0xffe0b341 };
const juce::Colour bad { 0xffd9534f };
} // namespace colours

class StudioLook : public juce::LookAndFeel_V4
{
public:
    StudioLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int depth) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override {}
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
    juce::Font getTextButtonFont(juce::TextButton&, int h) override;
    juce::Font getLabelFont(juce::Label&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawProgressBar(juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String& text) override;
};

juce::Font uiFont(float height, bool bold = false);

// A titled panel background
void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {});
} // namespace pnam::ui
