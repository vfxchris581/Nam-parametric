#include "PlayPage.h"

#include "CapturePlanner.h"
#include "Validation.h"

namespace pnam::ui
{
// ============================================================================ AmpFace
AmpFace::AmpFace(AppContext& ctx) : ctx_(ctx) { rebuild(); }

void AmpFace::rebuild()
{
    knobs_.clear();
    switches_.clear();
    switchCaptions_.clear();
    knobIndexForControl_.clear();
    isSwitch_.clear();
    const auto* info = ctx_.engine.modelInfo();
    if (!info)
    {
        title_ = {};
        repaint();
        return;
    }
    title_ = info->displayName();
    juce::StringArray sub;
    if (!info->gearMake.empty()) sub.add(info->gearMake);
    if (!info->modeledBy.empty()) sub.add("captured by " + juce::String(info->modeledBy));
    sub.add(juce::String(info->knobs.size()) + " parametric controls");
    sub.add(juce::String(info->sampleRate / 1000.0, 1) + " kHz");
    subtitle_ = sub.joinIntoString("  ·  ");
    // validation badge from the training report embedded by the trainer
    badge_ = "Validation: unknown";
    badgeColour_ = colours::textDim;
    try
    {
        auto j = readJsonFile(toPath(ctx_.engine.modelFile()));
        const auto t = j.value("metadata", nlohmann::json::object()).value("training", nlohmann::json::object());
        if (t.contains("report"))
        {
            const auto& r = t["report"];
            const bool unseen = r.value("validated_on_unseen_settings", false);
            const double e = unseen ? r.value("mean_holdout_esr", -1.0) : r.value("mean_trained_esr", -1.0);
            const auto v = esrVerdict(e);
            badge_ = unseen ? "Unseen settings: " + juce::String(v) + "  (ESR " + juce::String(e, 3) + ")"
                            : "Not validated on unseen settings · fit " + juce::String(v);
            badgeColour_ = !unseen ? colours::warn : (e <= 0.03 ? colours::good : (e <= 0.08 ? colours::warn : colours::bad));
        }
    }
    catch (...)
    {
    }

    for (size_t i = 0; i < info->knobs.size(); ++i)
    {
        const auto& k = info->knobs[i];
        knobIndexForControl_.push_back((int) i);
        isSwitch_.push_back(k.isSwitch());
        if (k.isSwitch())
        {
            auto* b = switches_.add(new juce::TextButton());
            auto* cap = switchCaptions_.add(new juce::Label({}, juce::String(k.name).toUpperCase()));
            styleLabel(*cap, 12.0f, true);
            cap->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(cap);
            const int n = (int) k.labels.size();
            auto update = [b, k, n, this, i] {
                const int pos = juce::roundToInt(ctx_.engine.knob((int) i) * (float) (n - 1));
                b->setButtonText(juce::String(k.labels[(size_t) juce::jlimit(0, n - 1, pos)]).toUpperCase());
                b->setToggleState(pos > 0, juce::dontSendNotification);
            };
            b->onClick = [this, i, n, update] {
                const int pos = juce::roundToInt(ctx_.engine.knob((int) i) * (float) (n - 1));
                ctx_.engine.setKnob((int) i, (float) ((pos + 1) % n) / (float) (n - 1));
                update();
            };
            update();
            addAndMakeVisible(b);
        }
        else
        {
            auto* kn = knobs_.add(new LabelledKnob(k.name, true));
            auto& s = kn->slider;
            s.setRange(0.0, 1.0, 0.0);
            s.setDoubleClickReturnValue(true, k.defaultValue);
            s.setValue(ctx_.engine.knob((int) i), juce::dontSendNotification);
            s.textFromValueFunction = [k](double v) { return juce::String(toDisplay(k, (float) v), 1); };
            s.valueFromTextFunction = [k](const juce::String& t) { return (double) fromDisplay(k, t.getFloatValue()); };
            s.onValueChange = [this, i, &s] { ctx_.engine.setKnob((int) i, (float) s.getValue()); };
            s.updateText();
            addAndMakeVisible(kn);
        }
    }
    resized();
    repaint();
}

void AmpFace::syncFromEngine()
{
    int ki = 0;
    for (size_t c = 0; c < isSwitch_.size(); ++c)
        if (!isSwitch_[c])
            knobs_[ki++]->slider.setValue(ctx_.engine.knob(knobIndexForControl_[c]), juce::dontSendNotification);
    int si = 0;
    const auto* info = ctx_.engine.modelInfo();
    for (size_t c = 0; c < isSwitch_.size(); ++c)
        if (isSwitch_[c] && info)
        {
            const auto& k = info->knobs[(size_t) knobIndexForControl_[c]];
            const int n = (int) k.labels.size();
            const int pos = juce::roundToInt(ctx_.engine.knob(knobIndexForControl_[c]) * (float) (n - 1));
            switches_[si]->setButtonText(juce::String(k.labels[(size_t) juce::jlimit(0, n - 1, pos)]).toUpperCase());
            switches_[si++]->setToggleState(pos > 0, juce::dontSendNotification);
        }
}

void AmpFace::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // cabinet-style tolex frame
    g.setColour(juce::Colour(0xff0b0b0c));
    g.fillRoundedRectangle(r, 14.0f);
    juce::Random rnd(7);
    g.setColour(juce::Colours::white.withAlpha(0.018f));
    for (int i = 0; i < 900; ++i)
        g.fillRect(r.getX() + rnd.nextFloat() * r.getWidth(), r.getY() + rnd.nextFloat() * r.getHeight(), 1.5f, 1.5f);
    // faceplate
    auto plate = r.reduced(16.0f, 18.0f);
    juce::ColourGradient metal(juce::Colour(0xff2b2d31), plate.getX(), plate.getY(), juce::Colour(0xff1a1b1e), plate.getX(), plate.getBottom(), false);
    g.setGradientFill(metal);
    g.fillRoundedRectangle(plate, 8.0f);
    g.setColour(juce::Colours::white.withAlpha(0.03f));
    for (float y = plate.getY() + 2; y < plate.getBottom(); y += 3.0f)
        g.drawHorizontalLine((int) y, plate.getX() + 4, plate.getRight() - 4);
    g.setColour(colours::accent.withAlpha(0.85f));
    g.drawRoundedRectangle(plate.reduced(5.0f), 6.0f, 1.2f); // gold piping

    auto head = plate.reduced(26.0f, 14.0f).removeFromTop(46.0f);
    if (title_.isEmpty())
    {
        g.setColour(colours::textDim);
        g.setFont(uiFont(18.0f, true));
        g.drawText("No parametric model loaded", plate, juce::Justification::centred);
        g.setFont(uiFont(13.0f));
        g.drawText("Open the Library, or build one from your NAM captures in the Capture Studio.", plate.translated(0, 28),
                   juce::Justification::centred);
        return;
    }
    // pilot lamp
    auto lamp = juce::Rectangle<float>(14, 14).withCentre({ head.getRight() - 8, head.getCentreY() - 6 });
    g.setColour(colours::accent.withAlpha(0.25f));
    g.fillEllipse(lamp.expanded(6));
    g.setColour(colours::accent);
    g.fillEllipse(lamp);
    g.setColour(colours::text);
    g.setFont(uiFont(24.0f, true));
    g.drawText(title_, head.removeFromTop(28), juce::Justification::left);
    g.setColour(colours::textDim);
    g.setFont(uiFont(12.5f));
    g.drawText(subtitle_, head, juce::Justification::left);
    // badge
    auto br = plate.reduced(26.0f, 14.0f).removeFromBottom(22.0f).removeFromRight(420.0f);
    g.setColour(badgeColour_.withAlpha(0.15f));
    g.fillRoundedRectangle(br, 11.0f);
    g.setColour(badgeColour_);
    g.setFont(uiFont(12.0f, true));
    g.drawText(badge_, br, juce::Justification::centred);
    g.setColour(colours::textDim.withAlpha(0.7f));
    g.setFont(uiFont(11.0f, true));
    g.drawText("PARAMETRIC NAM", plate.reduced(26.0f, 14.0f).removeFromBottom(22.0f), juce::Justification::left);
}

void AmpFace::resized()
{
    auto r = getLocalBounds().reduced(16, 18).reduced(26, 14);
    r.removeFromTop(56);
    r.removeFromBottom(30);
    const int n = (int) isSwitch_.size();
    if (n == 0)
        return;
    const int w = juce::jmin(170, r.getWidth() / n);
    auto row = r.withSizeKeepingCentre(w * n, juce::jmin(r.getHeight(), 210));
    int ki = 0, si = 0;
    for (int c = 0; c < n; ++c)
    {
        auto cell = row.removeFromLeft(w).reduced(6, 0);
        if (isSwitch_[(size_t) c])
        {
            switchCaptions_[si]->setBounds(cell.removeFromTop(22));
            switches_[si++]->setBounds(cell.withSizeKeepingCentre(juce::jmin(84, cell.getWidth()), 34));
        }
        else
            knobs_[ki++]->setBounds(cell);
    }
}

// ============================================================================ IRStrip
IRStrip::IRStrip(AppContext& ctx, int slot) : ctx_(ctx), slot_(slot)
{
    for (auto* c : std::initializer_list<juce::Component*> { &enable_, &invert_, &load_, &clear_, &name_, &details_, &level_, &pan_ })
        addAndMakeVisible(c);
    styleLabel(name_, 14.0f, true);
    styleLabel(details_, 11.5f, false, colours::textDim);
    level_.slider.setRange(-24.0, 12.0, 0.1);
    level_.slider.setDoubleClickReturnValue(true, 0.0);
    level_.slider.setTextValueSuffix(" dB");
    pan_.slider.setRange(-1.0, 1.0, 0.01);
    pan_.slider.setDoubleClickReturnValue(true, 0.0);
    pan_.slider.textFromValueFunction = [](double v) {
        return std::abs(v) < 0.005 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(juce::roundToInt(std::abs(v) * 100));
    };
    for (auto* s : { &level_.slider, &pan_.slider })
        s->onValueChange = [this] { push(); };
    enable_.onClick = invert_.onClick = [this] { push(); };
    load_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (!f.existsAsFile())
                return;
            auto err = ctx_.engine.loadIR(slot_, f);
            if (err.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Impulse response", err);
            refresh();
        });
    };
    clear_.onClick = [this] {
        ctx_.engine.cabinet().clearIR(slot_);
        auto s = slot_ == 0 ? ctx_.engine.state().irA : ctx_.engine.state().irB;
        s.path.clear();
        s.enabled = false;
        ctx_.engine.setIR(slot_, s);
        refresh();
    };
    refresh();
}

void IRStrip::push()
{
    IRSlotSettings s = slot_ == 0 ? ctx_.engine.state().irA : ctx_.engine.state().irB;
    s.enabled = enable_.getToggleState();
    s.invert = invert_.getToggleState();
    s.levelDb = (float) level_.slider.getValue();
    s.pan = (float) pan_.slider.getValue();
    ctx_.engine.setIR(slot_, s);
}

void IRStrip::refresh()
{
    const auto& s = slot_ == 0 ? ctx_.engine.state().irA : ctx_.engine.state().irB;
    const auto in = ctx_.engine.cabinet().info(slot_);
    enable_.setToggleState(s.enabled, juce::dontSendNotification);
    invert_.setToggleState(s.invert, juce::dontSendNotification);
    level_.slider.setValue(s.levelDb, juce::dontSendNotification);
    pan_.slider.setValue(s.pan, juce::dontSendNotification);
    name_.setText(in.loaded() ? in.name : juce::String("No IR loaded"), juce::dontSendNotification);
    details_.setText(in.loaded() ? juce::String(in.samples) + " samples · " + juce::String(in.sampleRate / 1000.0, 1) + " kHz · "
                                       + (in.channels > 1 ? "stereo (summed)" : "mono")
                                 : juce::String("WAV / AIFF / FLAC, resampled automatically"),
                     juce::dontSendNotification);
    repaint();
}

void IRStrip::paint(juce::Graphics& g) { drawPanel(g, getLocalBounds().toFloat(), slot_ == 0 ? "IR A" : "IR B"); }

void IRStrip::resized()
{
    auto r = getLocalBounds().reduced(14, 10);
    r.removeFromTop(18);
    auto knobs = r.removeFromRight(170).withSizeKeepingCentre(170, juce::jmin(r.getHeight(), 110));
    level_.setBounds(knobs.removeFromLeft(85));
    pan_.setBounds(knobs);
    name_.setBounds(r.removeFromTop(22));
    details_.setBounds(r.removeFromTop(18));
    r.removeFromTop(6);
    auto row = r.removeFromTop(28);
    load_.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(6);
    clear_.setBounds(row.removeFromLeft(60));
    row.removeFromLeft(10);
    enable_.setBounds(row.removeFromLeft(70));
    invert_.setBounds(row.removeFromLeft(90));
}

// ============================================================================ PlayPage
PlayPage::PlayPage(AppContext& ctx)
    : ctx_(ctx), face_(ctx), inMeter_([&ctx] { return ctx.engine.amp().inputMeter.takePeak(); }, "IN"),
      outMeter_([&ctx] { return ctx.engine.takeOutputPeak(); }, "OUT"), irA_(ctx, 0), irB_(ctx, 1)
{
    for (auto* c : std::initializer_list<juce::Component*> { &face_, &inMeter_, &outMeter_, &inGain_, &outGain_, &gateThresh_, &gateOn_,
                                                             &normalize_, &cabOn_, &stereo_, &irA_, &irB_, &abButton_, &abLabel_, &cabWarning_ })
        addAndMakeVisible(c);
    inGain_.slider.setRange(-24.0, 24.0, 0.1);
    outGain_.slider.setRange(-40.0, 12.0, 0.1);
    gateThresh_.slider.setRange(-100.0, -20.0, 0.5);
    for (auto* s : { &inGain_.slider, &outGain_.slider, &gateThresh_.slider })
        s->setTextValueSuffix(" dB");
    inGain_.slider.setDoubleClickReturnValue(true, 0.0);
    outGain_.slider.setDoubleClickReturnValue(true, 0.0);
    inGain_.slider.onValueChange = [this] { ctx_.engine.setInputGainDb((float) inGain_.slider.getValue()); };
    outGain_.slider.onValueChange = [this] { ctx_.engine.setOutputGainDb((float) outGain_.slider.getValue()); };
    gateThresh_.slider.onValueChange = gateOn_.onClick = [this] {
        ctx_.engine.setGate(gateOn_.getToggleState(), (float) gateThresh_.slider.getValue());
    };
    normalize_.onClick = [this] { ctx_.engine.setNormalize(normalize_.getToggleState()); };
    cabOn_.onClick = [this] { ctx_.engine.setCabEnabled(cabOn_.getToggleState()); };
    stereo_.onClick = [this] { ctx_.engine.setStereo(stereo_.getToggleState()); };
    abButton_.setClickingTogglesState(true);
    abButton_.onClick = [this] {
        ctx_.engine.setListenToReference(abButton_.getToggleState());
        abButton_.setButtonText(abButton_.getToggleState() ? "B: Source capture" : "A: Parametric model");
    };
    styleLabel(abLabel_, 12.0f, false, colours::textDim);
    abButton_.setVisible(false);
    abLabel_.setVisible(false);
    styleLabel(cabWarning_, 12.0f, true, colours::warn);
    syncFromEngine();
    ctx_.modelChanged.addChangeListener(this);
    startTimerHz(4);
}

PlayPage::~PlayPage() { ctx_.modelChanged.removeChangeListener(this); }

void PlayPage::syncFromEngine()
{
    const auto& s = ctx_.engine.state();
    inGain_.slider.setValue(s.inputGainDb, juce::dontSendNotification);
    outGain_.slider.setValue(s.outputGainDb, juce::dontSendNotification);
    gateThresh_.slider.setValue(s.gateThresholdDb, juce::dontSendNotification);
    gateOn_.setToggleState(s.gateOn, juce::dontSendNotification);
    normalize_.setToggleState(s.normalizeLoudness, juce::dontSendNotification);
    cabOn_.setToggleState(s.cabEnabled, juce::dontSendNotification);
    stereo_.setToggleState(s.stereo, juce::dontSendNotification);
    face_.syncFromEngine();
    irA_.refresh();
    irB_.refresh();
}

void PlayPage::changeListenerCallback(juce::ChangeBroadcaster*)
{
    face_.rebuild();
    syncFromEngine();
    resized();
}

void PlayPage::timerCallback()
{
    const bool ref = ctx_.engine.hasReference();
    abButton_.setVisible(ref);
    abLabel_.setVisible(ref);
    if (ref)
        abLabel_.setText("A/B against " + ctx_.engine.referenceFile().getFileName() + " (set the controls to its captured positions)",
                         juce::dontSendNotification);
    // warn about double cabinets: model trained from captures that already contain a cab
    juce::String warn;
    if (const auto* info = ctx_.engine.modelInfo())
        if (info->cabFromMetadata == NamFileInfo::Cab::yes && (ctx_.engine.state().irA.enabled || ctx_.engine.state().irB.enabled)
            && ctx_.engine.state().cabEnabled)
            warn = "This model already includes a cabinet - an IR on top doubles it.";
    cabWarning_.setText(warn, juce::dontSendNotification);
}

void PlayPage::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    drawPanel(g, inMeter_.getBounds().getUnion(inGain_.getBounds()).getUnion(gateOn_.getBounds()).expanded(10).toFloat(), "Input");
    drawPanel(g, outMeter_.getBounds().getUnion(outGain_.getBounds()).getUnion(stereo_.getBounds()).expanded(10).toFloat(), "Output");
    auto cabHead = irA_.getBounds().getUnion(irB_.getBounds()).withHeight(28).translated(0, -34);
    g.setColour(colours::text);
    g.setFont(uiFont(15.0f, true));
    g.drawText("CABINET", cabHead.withTrimmedLeft(4), juce::Justification::left);
}

void PlayPage::resized()
{
    auto r = getLocalBounds().reduced(18);
    auto top = r.removeFromTop(juce::jmax(320, r.getHeight() * 66 / 100));
    auto left = top.removeFromLeft(150).reduced(10);
    left.removeFromTop(18);
    auto right = top.removeFromRight(150).reduced(10);
    right.removeFromTop(18);
    top.reduce(12, 0);
    face_.setBounds(top);

    gateOn_.setBounds(left.removeFromBottom(24));
    auto lk = left.removeFromBottom(juce::jmin(200, left.getHeight() / 2));
    inGain_.setBounds(lk.removeFromTop(lk.getHeight() / 2));
    gateThresh_.setBounds(lk);
    inMeter_.setBounds(left.reduced(0, 4));

    stereo_.setBounds(right.removeFromBottom(24));
    normalize_.setBounds(right.removeFromBottom(24));
    outGain_.setBounds(right.removeFromBottom(juce::jmin(100, right.getHeight() / 2)));
    outMeter_.setBounds(right.reduced(0, 4));

    r.removeFromTop(14);
    auto ab = r.removeFromBottom(34);
    abButton_.setBounds(ab.removeFromLeft(200).reduced(0, 3));
    abLabel_.setBounds(ab.withTrimmedLeft(10));
    auto cabHead = r.removeFromTop(30);
    cabOn_.setBounds(cabHead.removeFromRight(120));
    cabWarning_.setBounds(cabHead.withTrimmedLeft(110));
    r.removeFromTop(6);
    irA_.setBounds(r.removeFromLeft(r.getWidth() / 2).withTrimmedRight(8));
    irB_.setBounds(r.withTrimmedLeft(8));
}
} // namespace pnam::ui
