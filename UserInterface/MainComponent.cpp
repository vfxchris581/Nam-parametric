#include "MainComponent.h"

namespace pnam::ui
{
// ============================================================================ SettingsPage
SettingsPage::SettingsPage(AppContext& ctx)
    : ctx_(ctx), selector_(ctx.engine.deviceManager(), 1, 8, 2, 2, false, false, false, false)
{
    addAndMakeVisible(selector_);
    addAndMakeVisible(inputChannel_);
    addAndMakeVisible(inputLabel_);
    addAndMakeVisible(about_);
    addAndMakeVisible(openData_);
    inputLabel_.setText("Guitar input channel", juce::dontSendNotification);
    styleLabel(inputLabel_, 13.0f, true);
    for (int i = 1; i <= 8; ++i)
        inputChannel_.addItem("Input " + juce::String(i), i);
    inputChannel_.setSelectedId(ctx_.engine.inputChannel() + 1, juce::dontSendNotification);
    inputChannel_.onChange = [this] { ctx_.engine.setInputChannel(inputChannel_.getSelectedId() - 1); };
    openData_.onClick = [this] { toFile(ctx_.dataDir).startAsProcess(); };
    styleLabel(about_, 12.5f, false, colours::textDim);
    about_.setJustificationType(juce::Justification::topLeft);
    about_.setText("Parametric NAM Studio " PNAM_VERSION_STRING "\n\n"
                   "Every playable amp is a Parametric NAM model: one neural network conditioned on the amp's controls, trained from "
                   "several NAM captures of the same amplifier. Plain NAM captures are training material and A/B references only.\n\n"
                   "Models run at their training sample rate with band-limited conversion to and from the interface rate. "
                   "Built on NeuralAmpModelerCore (MIT) and the ParametricWaveNet file format from parametric-nam (MIT); see THIRD_PARTY_NOTICES.md.\n\n"
                   "Data folder: " + juce::String(toUtf8(ctx_.dataDir)),
                   juce::dontSendNotification);
}

void SettingsPage::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    drawPanel(g, selector_.getBounds().expanded(12).withTrimmedTop(-22).toFloat(), "Audio device");
    drawPanel(g, about_.getBounds().getUnion(inputChannel_.getBounds()).expanded(12).withTrimmedTop(-22).toFloat(), "Signal & about");
}

void SettingsPage::resized()
{
    auto r = getLocalBounds().reduced(30);
    auto left = r.removeFromLeft(juce::jmin(600, r.getWidth() / 2));
    left.removeFromTop(26);
    selector_.setBounds(left.reduced(10));
    r.removeFromLeft(30);
    r.removeFromTop(26);
    r.reduce(10, 10);
    inputLabel_.setBounds(r.removeFromTop(22));
    inputChannel_.setBounds(r.removeFromTop(28).removeFromLeft(200));
    r.removeFromTop(16);
    openData_.setBounds(r.removeFromBottom(30).removeFromLeft(170));
    about_.setBounds(r);
}

// ============================================================================ HeaderBar
HeaderBar::HeaderBar(AppContext& ctx, std::function<void()> onPresetLoaded) : ctx_(ctx), onPresetLoaded_(std::move(onPresetLoaded))
{
    addAndMakeVisible(presets_);
    addAndMakeVisible(presetActions_);
    addAndMakeVisible(save_);
    presets_.setTextWhenNothingSelected("Presets");
    presets_.onChange = [this] {
        auto list = ctx_.presets.list();
        const int i = presets_.getSelectedItemIndex();
        if (i < 0 || i >= (int) list.size())
            return;
        try
        {
            auto s = PresetState::load(list[(size_t) i]);
            current_ = toFile(list[(size_t) i]);
            ctx_.engine.applyState(s, [this](juce::String problems) {
                ctx_.modelChanged.sendChangeMessage();
                if (onPresetLoaded_)
                    onPresetLoaded_();
                if (problems.trim().isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset", problems);
            });
        }
        catch (const std::exception& e)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset", e.what());
        }
    };
    save_.onClick = [this] {
        if (current_.existsAsFile())
        {
            auto s = ctx_.engine.captureState();
            s.name = current_.getFileNameWithoutExtension().toStdString();
            s.save(toPath(current_));
        }
        else
            savePresetAs({});
    };
    presetActions_.onClick = [this] { presetMenu(); };
    ctx_.presetsChanged.addChangeListener(this);
    refreshPresets();
    timerCallback();
    startTimerHz(2);
}

void HeaderBar::setCurrentPreset(const juce::File& f)
{
    current_ = f;
    refreshPresets();
}

HeaderBar::~HeaderBar() { ctx_.presetsChanged.removeChangeListener(this); }

void HeaderBar::refreshPresets()
{
    presets_.clear(juce::dontSendNotification);
    auto list = ctx_.presets.list();
    for (size_t i = 0; i < list.size(); ++i)
    {
        presets_.addItem(toFile(list[i]).getFileNameWithoutExtension(), (int) i + 1);
        if (toFile(list[i]) == current_)
            presets_.setSelectedId((int) i + 1, juce::dontSendNotification);
    }
}

void HeaderBar::savePresetAs(const juce::String& suggested)
{
    auto* w = new juce::AlertWindow("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", suggested.isNotEmpty() ? suggested : juce::String(ctx_.engine.modelInfo() ? ctx_.engine.modelInfo()->displayName() : "Preset"));
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
                           if (r != 1)
                               return;
                           auto s = ctx_.engine.captureState();
                           s.name = w->getTextEditorContents("name").trim().toStdString();
                           current_ = toFile(ctx_.presets.save(s));
                           refreshPresets();
                       }),
                       true);
}

void HeaderBar::presetMenu()
{
    juce::PopupMenu m;
    const bool have = current_.existsAsFile();
    m.addItem(1, "Save as...");
    m.addItem(2, "Rename...", have);
    m.addItem(3, "Duplicate", have);
    m.addItem(4, "Delete", have);
    m.addSeparator();
    m.addItem(5, "Import preset...");
    m.addItem(6, "Export preset...", have);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetActions_), [this](int r) {
        try
        {
            if (r == 1)
                savePresetAs({});
            else if (r == 2)
            {
                auto* w = new juce::AlertWindow("Rename preset", "New name:", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor("name", current_.getFileNameWithoutExtension());
                w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
                w->addButton("Cancel", 0);
                w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int rr) {
                                       if (rr == 1)
                                       {
                                           try
                                           {
                                               current_ = toFile(ctx_.presets.rename(toPath(current_), w->getTextEditorContents("name").trim().toStdString()));
                                           }
                                           catch (const std::exception& e)
                                           {
                                               juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Rename", e.what());
                                           }
                                           refreshPresets();
                                       }
                                   }),
                                   true);
            }
            else if (r == 3)
            {
                current_ = toFile(ctx_.presets.duplicate(toPath(current_)));
                refreshPresets();
            }
            else if (r == 4)
            {
                ctx_.presets.remove(toPath(current_));
                current_ = juce::File();
                refreshPresets();
            }
            else if (r == 5)
            {
                chooser_ = std::make_unique<juce::FileChooser>("Import preset", juce::File(), "*.pnampreset");
                chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
                    if (fc.getResult().existsAsFile())
                    {
                        try
                        {
                            current_ = toFile(ctx_.presets.importFile(toPath(fc.getResult())));
                        }
                        catch (const std::exception& e)
                        {
                            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import preset", e.what());
                        }
                        refreshPresets();
                    }
                });
            }
            else if (r == 6)
            {
                chooser_ = std::make_unique<juce::FileChooser>("Export preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(current_.getFileName()),
                                                               "*.pnampreset");
                chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
                    if (fc.getResult() != juce::File())
                        PresetBank::exportFile(toPath(current_), toPath(fc.getResult()));
                });
            }
        }
        catch (const std::exception& e)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset", e.what());
        }
    });
}

void HeaderBar::timerCallback()
{
    auto& dm = ctx_.engine.deviceManager();
    juce::String s;
    if (auto* d = dm.getCurrentAudioDevice())
    {
        const double sr = d->getCurrentSampleRate();
        const int lat = ctx_.engine.totalLatencySamples() + d->getInputLatencyInSamples() + d->getOutputLatencyInSamples() + d->getCurrentBufferSizeSamples();
        s << juce::String(sr / 1000.0, 1) << " kHz  ·  " << d->getCurrentBufferSizeSamples() << " samples  ·  latency "
          << juce::String(1000.0 * lat / sr, 1) << " ms  ·  CPU " << juce::roundToInt(dm.getCpuUsage() * 100) << "%";
    }
    else
        s = "No audio device - choose one in Settings";
    if (s != status_)
    {
        status_ = s;
        repaint();
    }
}

void HeaderBar::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0e0f11));
    g.fillRect(r);
    g.setColour(colours::outline);
    g.drawHorizontalLine(getHeight() - 1, 0, (float) getWidth());
    auto brand = r.reduced(20, 0).removeFromLeft(330);
    // logo mark: a stylised knob arc
    auto mark = brand.removeFromLeft(34).withSizeKeepingCentre(26, 26);
    juce::Path arc;
    arc.addCentredArc(mark.getCentreX(), mark.getCentreY(), 11, 11, 0, -2.4f, 2.4f, true);
    g.setColour(colours::accent);
    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillEllipse(mark.withSizeKeepingCentre(7, 7));
    g.setColour(colours::text);
    g.setFont(uiFont(17.0f, true));
    g.drawText("PARAMETRIC NAM", brand.removeFromLeft(160), juce::Justification::centredLeft);
    g.setColour(colours::accent);
    g.setFont(uiFont(17.0f));
    g.drawText("STUDIO", brand, juce::Justification::centredLeft);
    g.setColour(colours::textDim);
    g.setFont(uiFont(12.0f));
    g.drawText(status_, r.reduced(20, 0), juce::Justification::centredRight);
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced(0, 10);
    auto mid = r.withSizeKeepingCentre(420, r.getHeight());
    presets_.setBounds(mid.removeFromLeft(250));
    mid.removeFromLeft(8);
    save_.setBounds(mid.removeFromLeft(64));
    mid.removeFromLeft(6);
    presetActions_.setBounds(mid.removeFromLeft(90));
}

// ============================================================================ MainComponent
MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel(&look_);
    header_ = std::make_unique<HeaderBar>(ctx_, [this] { play_->syncFromEngine(); });
    play_ = std::make_unique<PlayPage>(ctx_);
    library_ = std::make_unique<LibraryPage>(ctx_);
    studio_ = std::make_unique<StudioPage>(ctx_);
    settings_ = std::make_unique<SettingsPage>(ctx_);
    tabs_.setTabBarDepth(40);
    tabs_.setOutline(0);
    tabs_.addTab("Amp", colours::background, play_.get(), false);
    tabs_.addTab("Library", colours::background, library_.get(), false);
    tabs_.addTab("Capture Studio", colours::background, studio_.get(), false);
    tabs_.addTab("Settings", colours::background, settings_.get(), false);
    addAndMakeVisible(*header_);
    addAndMakeVisible(tabs_);
    // reopen the last model
    auto last = ctx_.settings->getValue("lastModel");
    if (last.isNotEmpty() && juce::File(last).existsAsFile())
        ctx_.engine.loadModel(juce::File(last), [this](juce::String err) {
            if (err.isEmpty())
                ctx_.modelChanged.sendChangeMessage();
        });
    setSize(1440, 900);
}

MainComponent::~MainComponent()
{
    if (ctx_.engine.hasModel())
        ctx_.settings->setValue("lastModel", ctx_.engine.modelFile().getFullPathName());
    if (auto xml = ctx_.engine.deviceManager().createStateXml())
        ctx_.settings->setValue("audioDevice", xml.get());
    ctx_.settings->saveIfNeeded();
    tabs_.clearTabs();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    header_->setBounds(r.removeFromTop(56));
    tabs_.setBounds(r);
}
} // namespace pnam::ui
