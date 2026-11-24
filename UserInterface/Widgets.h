#pragma once
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "StudioLook.h"

namespace pnam::ui
{
// Vertical peak meter with hold, fed by a function returning linear peak since last call.
class Meter : public juce::Component, private juce::Timer
{
public:
    explicit Meter(std::function<float()> source, juce::String label) : source_(std::move(source)), label_(std::move(label)) { startTimerHz(30); }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto lr = r.removeFromBottom(16.0f);
        g.setColour(colours::textDim);
        g.setFont(uiFont(10.5f, true));
        g.drawText(label_, lr, juce::Justification::centred);
        auto bar = r.withSizeKeepingCentre(juce::jmin(14.0f, r.getWidth()), r.getHeight());
        g.setColour(juce::Colour(0xff0c0d0f));
        g.fillRoundedRectangle(bar, 3.0f);
        auto norm = [](float db) { return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        const float h = bar.getHeight() * norm(levelDb_);
        auto fill = bar.withTop(bar.getBottom() - h);
        juce::ColourGradient grad(colours::good, bar.getCentreX(), bar.getBottom(), colours::bad, bar.getCentreX(), bar.getY(), false);
        grad.addColour(0.75, colours::warn);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(fill, 3.0f);
        const float hy = bar.getBottom() - bar.getHeight() * norm(holdDb_);
        g.setColour(colours::text);
        g.fillRect(bar.getX(), hy, bar.getWidth(), 1.5f);
        g.setColour(colours::textDim.withAlpha(0.4f));
        for (int db : { -6, -12, -24, -48 })
        {
            const float y = bar.getBottom() - bar.getHeight() * norm((float) db);
            g.drawHorizontalLine((int) y, bar.getRight() + 2, bar.getRight() + 5);
        }
    }

private:
    void timerCallback() override
    {
        const float pk = source_ ? source_() : 0.0f;
        const float db = juce::Decibels::gainToDecibels(pk, -100.0f);
        levelDb_ = db > levelDb_ ? db : levelDb_ - 1.2f;
        if (db > holdDb_) holdDb_ = db, holdFrames_ = 45;
        else if (--holdFrames_ < 0) holdDb_ -= 0.8f;
        repaint();
    }
    std::function<float()> source_;
    juce::String label_;
    float levelDb_ = -100, holdDb_ = -100;
    int holdFrames_ = 0;
};

// A labelled knob: slider + caption + value.
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob(juce::String caption, bool big) : caption_(std::move(caption)), big_(big)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setColour(juce::Slider::textBoxTextColourId, colours::text);
        addAndMakeVisible(slider);
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(colours::text.withAlpha(0.92f));
        g.setFont(uiFont(big_ ? 14.0f : 11.5f, true));
        g.drawText(caption_.toUpperCase(), getLocalBounds().removeFromTop(big_ ? 22 : 16), juce::Justification::centred);
    }
    void resized() override { slider.setBounds(getLocalBounds().withTrimmedTop(big_ ? 22 : 16)); }
    juce::Slider slider;

private:
    juce::String caption_;
    bool big_;
};

inline void styleLabel(juce::Label& l, float h = 13.0f, bool bold = false, juce::Colour c = colours::text)
{
    l.setFont(uiFont(h, bold));
    l.setColour(juce::Label::textColourId, c);
}
} // namespace pnam::ui
