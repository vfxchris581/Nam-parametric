#include "StudioLook.h"

namespace pnam::ui
{
juce::Font uiFont(float h, bool bold)
{
    return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain));
}

void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour(colours::panel);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour(colours::textDim);
        g.setFont(uiFont(11.5f, true));
        g.drawText(title.toUpperCase(), r.reduced(14, 8).removeFromTop(16), juce::Justification::left);
    }
}

StudioLook::StudioLook()
{
    using namespace colours;
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::DocumentWindow::backgroundColourId, background);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextButton::buttonColourId, panelRaised);
    setColour(juce::TextButton::buttonOnColourId, accentDim);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::ComboBox::backgroundColourId, panelRaised);
    setColour(juce::ComboBox::outlineColourId, outline);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::arrowColourId, textDim);
    setColour(juce::PopupMenu::backgroundColourId, panelRaised);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff15171b));
    setColour(juce::TextEditor::outlineColourId, outline);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::highlightColourId, accentDim);
    setColour(juce::CaretComponent::caretColourId, accent);
    setColour(juce::ListBox::backgroundColourId, panel);
    setColour(juce::ListBox::outlineColourId, outline);
    setColour(juce::TableHeaderComponent::backgroundColourId, panelRaised);
    setColour(juce::TableHeaderComponent::textColourId, textDim);
    setColour(juce::TableHeaderComponent::outlineColourId, outline);
    setColour(juce::ScrollBar::thumbColourId, outline.brighter(0.2f));
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::thumbColourId, accent);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::Slider::backgroundColourId, outline);
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ProgressBar::foregroundColourId, accent);
    setColour(juce::ProgressBar::backgroundColourId, panelRaised);
    setColour(juce::AlertWindow::backgroundColourId, panelRaised);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::TabbedComponent::backgroundColourId, background);
    setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::GroupComponent::outlineColourId, outline);
    setColour(juce::GroupComponent::textColourId, textDim);
}

juce::Font StudioLook::getTextButtonFont(juce::TextButton&, int h) { return uiFont(juce::jmin(14.0f, h * 0.55f), true); }
juce::Font StudioLook::getLabelFont(juce::Label& l) { return l.getFont(); }

void StudioLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4.0f);
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float rOuter = size * 0.5f, rKnob = rOuter * 0.74f;
    const float angle = start + pos * (end - start);
    const bool small = size < 60;

    // scale ticks (0..10 style)
    g.setColour(colours::textDim.withAlpha(0.6f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const float r1 = rOuter * 0.93f, r2 = rOuter * (i % 5 == 0 ? 1.0f : 0.97f);
        g.drawLine(c.x + r1 * std::sin(a), c.y - r1 * std::cos(a), c.x + r2 * std::sin(a), c.y - r2 * std::cos(a), i % 5 == 0 ? 1.6f : 1.0f);
    }
    // value arc
    const float arcR = rOuter * 0.84f;
    juce::Path track, value;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    value.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, start, angle, true);
    const float thick = small ? 2.5f : 3.5f;
    g.setColour(colours::outline);
    g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto acc = s.isEnabled() ? colours::accent : colours::textDim;
    g.setColour(acc);
    g.strokePath(value, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // glow
    g.setColour(acc.withAlpha(0.15f));
    g.strokePath(value, juce::PathStrokeType(thick * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body: knurled dark chrome
    const auto kb = juce::Rectangle<float>(rKnob * 2, rKnob * 2).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(kb.translated(0, size * 0.03f).expanded(1.5f));
    juce::ColourGradient body(juce::Colour(0xff3a3d44), c.x - rKnob * 0.5f, c.y - rKnob * 0.7f, juce::Colour(0xff0d0e10), c.x + rKnob * 0.6f,
                              c.y + rKnob, true);
    g.setGradientFill(body);
    g.fillEllipse(kb);
    if (!small)
    {
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        for (int i = 0; i < 36; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
            g.drawLine(c.x + rKnob * 0.9f * std::sin(a), c.y - rKnob * 0.9f * std::cos(a), c.x + rKnob * std::sin(a), c.y - rKnob * std::cos(a), 1.0f);
        }
    }
    juce::ColourGradient cap(juce::Colour(0xff2c2f35), c.x, c.y - rKnob * 0.6f, juce::Colour(0xff15161a), c.x, c.y + rKnob * 0.6f, false);
    g.setGradientFill(cap);
    g.fillEllipse(kb.reduced(rKnob * 0.18f));
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawEllipse(kb.reduced(rKnob * 0.18f), 1.0f);
    // pointer
    juce::Path p;
    const float pw = small ? 2.0f : 3.0f;
    p.addRoundedRectangle(-pw * 0.5f, -rKnob * 0.95f, pw, rKnob * 0.5f, pw * 0.5f);
    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.fillPath(p, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
}

void StudioLook::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st,
                                  juce::Slider& s)
{
    if (st != juce::Slider::LinearHorizontal && st != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, st, s);
        return;
    }
    const bool horiz = st == juce::Slider::LinearHorizontal;
    auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h);
    auto track = horiz ? r.withSizeKeepingCentre(r.getWidth(), 4.0f) : r.withSizeKeepingCentre(4.0f, r.getHeight());
    g.setColour(colours::outline);
    g.fillRoundedRectangle(track, 2.0f);
    g.setColour(colours::accent);
    if (horiz)
        g.fillRoundedRectangle(track.withRight(pos), 2.0f);
    else
        g.fillRoundedRectangle(track.withTop(pos), 2.0f);
    const auto thumb = horiz ? juce::Rectangle<float>(12, 12).withCentre({ pos, r.getCentreY() }) : juce::Rectangle<float>(12, 12).withCentre({ r.getCentreX(), pos });
    g.setColour(colours::text);
    g.fillEllipse(thumb);
}

void StudioLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    auto c = b.getToggleState() ? findColour(juce::TextButton::buttonOnColourId) : bg;
    if (down)
        c = c.brighter(0.15f);
    else if (over)
        c = c.brighter(0.07f);
    g.setColour(c);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(b.getToggleState() ? colours::accent : colours::outline);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

void StudioLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromLeft(34.0f).withSizeKeepingCentre(30.0f, 16.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? colours::accent : colours::outline.brighter(over ? 0.2f : 0.0f));
    g.fillRoundedRectangle(sw, 8.0f);
    g.setColour(on ? juce::Colours::white : colours::textDim);
    g.fillEllipse(juce::Rectangle<float>(12, 12).withCentre({ on ? sw.getRight() - 8 : sw.getX() + 8, sw.getCentreY() }));
    g.setColour(b.isEnabled() ? colours::text : colours::textDim);
    g.setFont(uiFont(13.0f));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(4), juce::Justification::centredLeft);
}

int StudioLook::getTabButtonBestWidth(juce::TabBarButton& b, int) { return juce::roundToInt(juce::GlyphArrangement::getStringWidth(uiFont(13.0f, true), b.getButtonText())) + 44; }

void StudioLook::drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool front = b.isFrontTab();
    g.setColour(front ? colours::text : (over ? colours::text.withAlpha(0.8f) : colours::textDim));
    g.setFont(uiFont(13.0f, true));
    g.drawText(b.getButtonText().toUpperCase(), r, juce::Justification::centred);
    if (front)
    {
        g.setColour(colours::accent);
        g.fillRoundedRectangle(r.removeFromBottom(3.0f).reduced(14.0f, 0), 1.5f);
    }
}

void StudioLook::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
    g.setColour(findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(box.hasKeyboardFocus(true) ? colours::accent : colours::outline);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour(colours::textDim);
    g.fillPath(arrow);
}

void StudioLook::drawProgressBar(juce::Graphics& g, juce::ProgressBar&, int w, int h, double progress, const juce::String& text)
{
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h);
    g.setColour(colours::panelRaised);
    g.fillRoundedRectangle(r, 4.0f);
    if (progress >= 0 && progress <= 1)
    {
        g.setColour(colours::accent);
        g.fillRoundedRectangle(r.withWidth((float) (w * progress)), 4.0f);
    }
    g.setColour(colours::text);
    g.setFont(uiFont(12.0f, true));
    g.drawText(text, r, juce::Justification::centred);
}
} // namespace pnam::ui
