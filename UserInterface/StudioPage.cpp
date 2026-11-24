#include "StudioPage.h"

#include <thread>

#include "Validation.h"

namespace pnam::ui
{
namespace
{
// A label that edits on double-click and reports the new text.
class EditCell : public juce::Label
{
public:
    std::function<void(const juce::String&)> onEdit;
    EditCell()
    {
        setEditable(false, true, false);
        setColour(juce::Label::textColourId, colours::text);
        setColour(juce::Label::textWhenEditingColourId, colours::text);
        setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff15171b));
        setFont(uiFont(13.0f));
        onTextChange = [this] {
            if (onEdit)
                onEdit(getText());
        };
    }
};

juce::String fmt(double v, int dp = 3) { return v < 0 ? juce::String("-") : juce::String(v, dp); }

juce::String settingText(const StudioProject& p, const std::vector<float>& v) { return describeSetting(p.knobs, v); }
} // namespace

// ============================================================================ CaptureTable
CaptureTable::CaptureTable(StudioPage& page, AppContext& ctx) : page_(page), ctx_(ctx)
{
    table_.setModel(this);
    table_.setRowHeight(28);
    table_.setMultipleSelectionEnabled(true);
    auto& h = table_.getHeader();
    h.addColumn("Capture", 1, 200);
    h.addColumn("Amplifier", 2, 130);
    h.addColumn("Rate", 3, 50);
    h.addColumn("Model", 4, 80);
    h.addColumn("Cabinet", 5, 70);
    for (auto* c : std::initializer_list<juce::Component*> { &table_, &ampFilter_, &importFiles_, &importFolder_, &setAmp_, &addToProject_, &remove_, &hint_,
                                                             &groupProblems_ })
        addAndMakeVisible(c);
    styleLabel(hint_, 12.0f, false, colours::textDim);
    hint_.setText("Drop .nam files or folders here. Captures are only grouped by amplifier from their metadata or by you - "
                  "never from similar file names.",
                  juce::dontSendNotification);
    hint_.setJustificationType(juce::Justification::topLeft);
    styleLabel(groupProblems_, 12.0f, true, colours::warn);
    groupProblems_.setJustificationType(juce::Justification::topLeft);
    ampFilter_.onChange = [this] { refresh(); };
    importFiles_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import NAM captures", juce::File(), "*.nam");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this](const juce::FileChooser& fc) {
                                  juce::StringArray p;
                                  for (auto& f : fc.getResults())
                                      p.add(f.getFullPathName());
                                  importPaths(p);
                              });
    };
    importFolder_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import a folder of NAM captures");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            if (fc.getResult().isDirectory())
                importPaths({ fc.getResult().getFullPathName() });
        });
    };
    setAmp_.onClick = [this] {
        auto sel = selection();
        if (sel.empty())
            return;
        auto* w = new juce::AlertWindow("Amplifier", "Which physical amplifier (and channel) were these " + juce::String(sel.size()) + " captures made from?",
                                        juce::MessageBoxIconType::QuestionIcon);
        w->addTextEditor("amp", juce::String(sel[0]->amp), "Amplifier");
        w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        std::vector<std::filesystem::path> paths;
        for (auto* e : sel)
            paths.push_back(e->path);
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, paths](int r) {
                               if (r == 1)
                               {
                                   const auto a = w->getTextEditorContents("amp").trim().toStdString();
                                   for (auto& p : paths)
                                       ctx_.captures.setAmp(p, a);
                                   ctx_.captures.save();
                                   refresh();
                               }
                           }),
                           true);
    };
    addToProject_.onClick = [this] { page_.addCapturesToProject(selection()); };
    remove_.onClick = [this] {
        for (auto* e : selection())
            ctx_.captures.remove(e->path);
        ctx_.captures.save();
        refresh();
    };
    refresh();
}

void CaptureTable::importPaths(const juce::StringArray& paths)
{
    std::vector<std::filesystem::path> ps;
    for (auto& p : paths)
        ps.push_back(toPath(juce::File(p)));
    auto r = ctx_.captures.importPaths(ps);
    ctx_.captures.save();
    refresh();
    juce::StringArray msg;
    msg.add(juce::String(r.added.size()) + " captures imported.");
    if (!r.duplicates.empty())
        msg.add(juce::String(r.duplicates.size()) + " skipped: identical to a capture already in the library (same weights).");
    if (!r.parametricSkipped.empty())
        msg.add(juce::String(r.parametricSkipped.size()) + " skipped: already parametric models (add them in the Library).");
    for (auto& [f, e] : r.failed)
        msg.add(juce::String(toUtf8(f.filename())) + ": " + e);
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Import", msg.joinIntoString("\n"));
}

void CaptureTable::filesDropped(const juce::StringArray& files, int, int) { importPaths(files); }

void CaptureTable::refresh()
{
    const auto keep = ampFilter_.getText();
    ampFilter_.clear(juce::dontSendNotification);
    ampFilter_.addItem("All amplifiers", 1);
    ampFilter_.addItem("Unassigned", 2);
    int id = 3;
    for (auto& a : ctx_.captures.ampNames())
        ampFilter_.addItem(a, id++);
    ampFilter_.setText(keep.isEmpty() ? "All amplifiers" : keep, juce::dontSendNotification);
    const auto f = ampFilter_.getText();
    rows_.clear();
    for (auto& e : ctx_.captures.entries())
        if (f == "All amplifiers" || (f == "Unassigned" && e.amp.empty()) || f == juce::String(e.amp))
            rows_.push_back(&e);
    table_.updateContent();
    table_.repaint();
    // problems within the shown amplifier group
    juce::StringArray probs;
    if (f != "All amplifiers" && f != "Unassigned")
        for (auto& p : CaptureLibrary::groupProblems(ctx_.captures.capturesOfAmp(f.toStdString())))
            probs.add(p);
    groupProblems_.setText(probs.joinIntoString("\n"), juce::dontSendNotification);
}

std::vector<const CaptureEntry*> CaptureTable::selection() const
{
    std::vector<const CaptureEntry*> v;
    auto rows = table_.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i] < (int) rows_.size())
            v.push_back(rows_[(size_t) rows[i]]);
    return v;
}

void CaptureTable::paintRowBackground(juce::Graphics& g, int row, int, int, bool sel)
{
    g.fillAll(sel ? colours::accentDim.withAlpha(0.55f) : (row % 2 ? colours::panel : colours::panel.brighter(0.02f)));
}

void CaptureTable::paintCell(juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row >= (int) rows_.size())
        return;
    const auto& e = *rows_[(size_t) row];
    juce::String t;
    g.setColour(colours::text);
    g.setFont(uiFont(12.5f, col == 1));
    switch (col)
    {
        case 1: t = e.info.name.empty() ? juce::String(toUtf8(e.path.stem())) : juce::String(e.info.name); break;
        case 2:
            t = e.amp.empty() ? juce::String("unassigned") : juce::String(e.amp);
            g.setColour(e.amp.empty() ? colours::textDim : (e.ampFromMetadata ? colours::good : colours::text));
            break;
        case 3: t = e.info.sampleRate > 0 ? juce::String(e.info.sampleRate / 1000.0, 1) + "k" : juce::String("48k?"); break;
        case 4: t = e.info.architecture; break;
        case 5:
            t = e.cab == CaptureEntry::Cab::yes ? "yes" : e.cab == CaptureEntry::Cab::likely ? "likely" : e.cab == CaptureEntry::Cab::no ? "no" : "?";
            g.setColour(e.cab == CaptureEntry::Cab::yes || e.cab == CaptureEntry::Cab::likely ? colours::warn : colours::textDim);
            break;
        default: break;
    }
    g.drawText(t, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

void CaptureTable::selectedRowsChanged(int)
{
    auto s = selection();
    if (s.size() == 1)
    {
        juce::StringArray l;
        l.add(juce::String(toUtf8(s[0]->path.filename())) + "  ·  " + s[0]->info.configSummary);
        if (s[0]->info.inputLevelDbu)
            l.add("Input calibration " + juce::String(*s[0]->info.inputLevelDbu, 1) + " dBu");
        if (!s[0]->cabReason.empty())
            l.add("Cabinet: " + juce::String(s[0]->cabReason));
        for (auto& w : s[0]->info.warnings)
            l.add(w);
        hint_.setText(l.joinIntoString("\n"), juce::dontSendNotification);
    }
}

void CaptureTable::paint(juce::Graphics& g) { drawPanel(g, getLocalBounds().toFloat(), "Source captures"); }

void CaptureTable::resized()
{
    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(22);
    auto bar = r.removeFromTop(30);
    importFiles_.setBounds(bar.removeFromLeft(140));
    bar.removeFromLeft(6);
    importFolder_.setBounds(bar.removeFromLeft(120));
    bar.removeFromLeft(6);
    ampFilter_.setBounds(bar);
    r.removeFromTop(8);
    hint_.setBounds(r.removeFromBottom(54));
    groupProblems_.setBounds(r.removeFromBottom(groupProblems_.getText().isEmpty() ? 0 : 36));
    auto actions = r.removeFromBottom(32);
    addToProject_.setBounds(actions.removeFromRight(150));
    setAmp_.setBounds(actions.removeFromLeft(130));
    actions.removeFromLeft(6);
    remove_.setBounds(actions.removeFromLeft(80));
    r.removeFromBottom(6);
    table_.setBounds(r);
}

// ============================================================================ table models
struct StudioPage::ControlsModel : juce::TableListBoxModel
{
    StudioPage& p;
    explicit ControlsModel(StudioPage& s) : p(s) {}
    int getNumRows() override { return (int) p.project_.knobs.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int, bool sel) override
    {
        g.fillAll(sel ? colours::accentDim.withAlpha(0.5f) : (row % 2 ? colours::panel : colours::panel.brighter(0.02f)));
    }
    void paintCell(juce::Graphics&, int, int, int, int, bool) override {}
    juce::Component* refreshComponentForCell(int row, int col, bool, juce::Component* existing) override
    {
        auto* c = dynamic_cast<EditCell*>(existing);
        if (!c)
        {
            delete existing;
            c = new EditCell();
        }
        if (row >= (int) p.project_.knobs.size())
            return c;
        auto& k = p.project_.knobs[(size_t) row];
        switch (col)
        {
            case 1: c->setText(k.name, juce::dontSendNotification); break;
            case 2: c->setText(k.isSwitch() ? juce::String("-") : juce::String(k.displayMin, 1), juce::dontSendNotification); break;
            case 3: c->setText(k.isSwitch() ? juce::String("-") : juce::String(k.displayMax, 1), juce::dontSendNotification); break;
            case 4: c->setText(juce::String(toDisplay(k, k.defaultValue), 1), juce::dontSendNotification); break;
            case 5:
            {
                juce::StringArray a;
                for (auto& l : k.labels)
                    a.add(l);
                c->setText(a.joinIntoString(", "), juce::dontSendNotification);
                break;
            }
            default: break;
        }
        c->onEdit = [this, row, col](const juce::String& t) {
            auto& kk = p.project_.knobs[(size_t) row];
            if (col == 1)
                kk.name = t.trim().toStdString();
            else if (col == 2)
                kk.displayMin = t.getFloatValue();
            else if (col == 3)
                kk.displayMax = t.getFloatValue();
            else if (col == 4)
                kk.defaultValue = fromDisplay(kk, t.getFloatValue());
            else if (col == 5)
            {
                kk.labels.clear();
                for (auto& s : juce::StringArray::fromTokens(t, ",", ""))
                    if (s.trim().isNotEmpty())
                        kk.labels.push_back(s.trim().toStdString());
                if (kk.labels.size() < 2)
                    kk.labels.clear();
            }
            p.projectChanged(true);
        };
        return c;
    }
};

struct StudioPage::AssignModel : juce::TableListBoxModel
{
    StudioPage& p;
    explicit AssignModel(StudioPage& s) : p(s) {}
    int getNumRows() override { return (int) p.project_.captures.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int, bool sel) override
    {
        g.fillAll(sel ? colours::accentDim.withAlpha(0.5f) : (row % 2 ? colours::panel : colours::panel.brighter(0.02f)));
    }
    void paintCell(juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        if (row >= (int) p.project_.captures.size())
            return;
        const auto& c = p.project_.captures[(size_t) row];
        if (col == 900)
        {
            g.setFont(uiFont(12.0f, true));
            g.setColour(!c.assigned ? colours::warn : (c.holdout ? colours::good : colours::textDim));
            g.drawText(!c.assigned ? "set positions" : (c.holdout ? "validation" : "training"), 6, 0, w - 8, h, juce::Justification::centredLeft);
        }
    }
    juce::Component* refreshComponentForCell(int row, int col, bool, juce::Component* existing) override
    {
        if (row >= (int) p.project_.captures.size() || col == 900)
        {
            delete existing;
            return nullptr;
        }
        auto& c = p.project_.captures[(size_t) row];
        if (col == 800) // hold-out toggle
        {
            auto* t = dynamic_cast<juce::ToggleButton*>(existing);
            if (!t)
            {
                delete existing;
                t = new juce::ToggleButton();
            }
            t->setToggleState(c.holdout, juce::dontSendNotification);
            t->onClick = [this, row, t] {
                p.project_.captures[(size_t) row].holdout = t->getToggleState();
                p.projectChanged(false);
            };
            return t;
        }
        auto* e = dynamic_cast<EditCell*>(existing);
        if (!e)
        {
            delete existing;
            e = new EditCell();
        }
        if (col == 1)
        {
            e->setText(c.label, juce::dontSendNotification);
            e->onEdit = [this, row](const juce::String& t) {
                p.project_.captures[(size_t) row].label = t.toStdString();
                p.projectChanged(false);
            };
        }
        else
        {
            const size_t k = (size_t) (col - 100);
            if (k < p.project_.knobs.size())
            {
                const auto& kn = p.project_.knobs[k];
                if (c.knobs01.size() != p.project_.knobs.size())
                    c.knobs01.resize(p.project_.knobs.size(), kn.defaultValue);
                const float v = c.knobs01[k];
                e->setText(!c.assigned ? juce::String("?")
                                       : kn.isSwitch() ? juce::String(kn.labels[(size_t) juce::roundToInt(v * (float) (kn.labels.size() - 1))])
                                                       : juce::String(toDisplay(kn, v), 1),
                           juce::dontSendNotification);
                e->setColour(juce::Label::textColourId, c.assigned ? colours::text : colours::warn);
                e->onEdit = [this, row, k](const juce::String& t) {
                    auto& cc = p.project_.captures[(size_t) row];
                    const auto& kk = p.project_.knobs[k];
                    if (kk.isSwitch())
                    {
                        int idx = -1;
                        for (size_t i = 0; i < kk.labels.size(); ++i)
                            if (juce::String(kk.labels[i]).equalsIgnoreCase(t.trim()))
                                idx = (int) i;
                        if (idx < 0)
                            idx = juce::jlimit(0, (int) kk.labels.size() - 1, t.getIntValue());
                        cc.knobs01[k] = (float) idx / (float) (kk.labels.size() - 1);
                    }
                    else
                        cc.knobs01[k] = fromDisplay(kk, t.getFloatValue());
                    if (!cc.assigned)
                    {
                        // first entry: start the other controls at their defaults
                        for (size_t j = 0; j < p.project_.knobs.size(); ++j)
                            if (j != k)
                                cc.knobs01[j] = p.project_.knobs[j].defaultValue;
                        cc.assigned = true;
                    }
                    p.projectChanged(false);
                };
            }
        }
        return e;
    }
};

struct StudioPage::ResultsModel : juce::TableListBoxModel
{
    StudioPage& p;
    explicit ResultsModel(StudioPage& s) : p(s) {}
    int getNumRows() override { return (int) p.lastReport_.captures.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int, bool) override { g.fillAll(row % 2 ? colours::panel : colours::panel.brighter(0.02f)); }
    void paintCell(juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        if (row >= (int) p.lastReport_.captures.size())
            return;
        const auto& c = p.lastReport_.captures[(size_t) row];
        juce::String t;
        g.setColour(colours::text);
        g.setFont(uiFont(12.5f));
        switch (col)
        {
            case 1: t = c.label; break;
            case 2: t = settingText(p.project_, c.knobs01); g.setColour(colours::textDim); break;
            case 3: t = c.holdout ? "held out (unseen)" : "trained"; g.setColour(c.holdout ? colours::good : colours::textDim); break;
            case 4: t = fmt(c.esr, 4); break;
            case 5:
                t = esrVerdict(c.esr);
                g.setColour(c.esr <= 0.03 ? colours::good : (c.esr <= 0.08 ? colours::warn : colours::bad));
                break;
            case 6: t = c.holdout ? fmt(c.nearestEsr, 4) + "  (" + juce::String(c.nearestLabel) + ")" : juce::String(); break;
            default: break;
        }
        g.drawText(t, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
    }
};

struct VersionsModel : juce::ListBoxModel
{
    StudioProject* project = nullptr;
    int getNumRows() override { return project ? (int) project->versions.size() : 0; }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (!project || row >= (int) project->versions.size())
            return;
        const auto& v = project->versions[project->versions.size() - 1 - (size_t) row];
        if (sel)
            g.fillAll(colours::accentDim.withAlpha(0.5f));
        g.setColour(colours::text);
        g.setFont(uiFont(13.0f, true));
        g.drawText("v" + juce::String(v.number), 8, 0, 40, h, juce::Justification::centredLeft);
        g.setFont(uiFont(12.0f));
        g.setColour(colours::textDim);
        g.drawText(juce::String(v.date) + "  ·  " + juce::String(v.architecture) + "  ·  " + juce::String(v.verdict), 50, 0, w - 56, h,
                   juce::Justification::centredLeft, true);
    }
};

// ============================================================================ StudioPage
StudioPage::StudioPage(AppContext& ctx) : ctx_(ctx), captures_(*this, ctx)
{
    controlsModel_ = std::make_unique<ControlsModel>(*this);
    assignModel_ = std::make_unique<AssignModel>(*this);
    resultsModel_ = std::make_unique<ResultsModel>(*this);
    auto vm = std::make_unique<VersionsModel>();
    vm->project = &project_;
    versionsModel_ = std::move(vm);

    for (auto* c : std::initializer_list<juce::Component*> { &captures_, &projectBox_, &newProject_, &saveProject_, &ampName_, &manufacturer_, &ampLabel_,
                                                             &makerLabel_, &tabs_ })
        addAndMakeVisible(c);
    ampLabel_.setText("Amplifier", juce::dontSendNotification);
    makerLabel_.setText("Manufacturer", juce::dontSendNotification);
    styleLabel(ampLabel_, 11.5f, true, colours::textDim);
    styleLabel(makerLabel_, 11.5f, true, colours::textDim);
    ampName_.onTextChange = [this] { project_.ampName = ampName_.getText().toStdString(); };
    manufacturer_.onTextChange = [this] { project_.manufacturer = manufacturer_.getText().toStdString(); };
    newProject_.onClick = [this] {
        auto* w = new juce::AlertWindow("New parametric model", "Name of the amplifier (and channel):", juce::MessageBoxIconType::QuestionIcon);
        w->addTextEditor("amp", "", "Amplifier");
        w->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
                               if (r == 1 && w->getTextEditorContents("amp").trim().isNotEmpty())
                                   newProject(w->getTextEditorContents("amp").trim());
                           }),
                           true);
    };
    saveProject_.onClick = [this] { saveProject(); };
    projectBox_.onChange = [this] {
        const int i = projectBox_.getSelectedItemIndex();
        auto files = ctx_.projectsDir().findChildFiles(juce::File::findFiles, false, "*.pnamproj");
        files.sort();
        if (i >= 0 && i < files.size() && files[i] != toFile(project_.file))
            openProject(files[i]);
    };

    tabs_.setTabBarDepth(34);
    tabs_.addTab("Controls & captures", colours::background, &controlsTab_, false);
    tabs_.addTab("Capture plan", colours::background, &planTab_, false);
    tabs_.addTab("Train & validate", colours::background, &trainTab_, false);

    // controls & captures tab
    controlsTable_.setModel(controlsModel_.get());
    controlsTable_.setRowHeight(26);
    auto& ch = controlsTable_.getHeader();
    ch.addColumn("Control", 1, 130, 60, 400, juce::TableHeaderComponent::notSortable);
    ch.addColumn("Dial min", 2, 70, 40, 100, juce::TableHeaderComponent::notSortable);
    ch.addColumn("Dial max", 3, 70, 40, 100, juce::TableHeaderComponent::notSortable);
    ch.addColumn("Default", 4, 70, 40, 100, juce::TableHeaderComponent::notSortable);
    ch.addColumn("Switch positions (comma separated, blank for a knob)", 5, 300, 60, 600, juce::TableHeaderComponent::notSortable);
    assignTable_.setModel(assignModel_.get());
    assignTable_.setRowHeight(26);
    assignTable_.setMultipleSelectionEnabled(true);
    for (auto* c : std::initializer_list<juce::Component*> { &controlsTable_, &assignTable_, &addControl_, &removeControl_, &removeCapture_, &audition_,
                                                             &controlsTitle_, &assignTitle_ })
        controlsTab_.addAndMakeVisible(c);
    controlsTitle_.setText("Controls of this amplifier (up to 8). Double-click a cell to edit.", juce::dontSendNotification);
    assignTitle_.setText("Control positions of each capture, as dialled on the amp. Hold-out captures are never trained on: they "
                         "measure how well the model predicts settings it has not seen.",
                         juce::dontSendNotification);
    styleLabel(controlsTitle_, 12.0f, false, colours::textDim);
    styleLabel(assignTitle_, 12.0f, false, colours::textDim);
    assignTitle_.setJustificationType(juce::Justification::topLeft);
    addControl_.onClick = [this] {
        if (project_.knobs.size() >= 8)
            return;
        KnobInfo k;
        const char* defaults[] = { "Gain", "Bass", "Middle", "Treble", "Presence", "Master", "Resonance", "Depth" };
        k.name = defaults[project_.knobs.size()];
        project_.knobs.push_back(k);
        for (auto& c : project_.captures)
            c.knobs01.push_back(k.defaultValue);
        projectChanged(true);
    };
    removeControl_.onClick = [this] {
        const int r = controlsTable_.getSelectedRow();
        if (r < 0 || r >= (int) project_.knobs.size())
            return;
        project_.knobs.erase(project_.knobs.begin() + r);
        for (auto& c : project_.captures)
            if ((int) c.knobs01.size() > r)
                c.knobs01.erase(c.knobs01.begin() + r);
        projectChanged(true);
    };
    removeCapture_.onClick = [this] {
        auto rows = assignTable_.getSelectedRows();
        for (int i = rows.size() - 1; i >= 0; --i)
            if (rows[i] < (int) project_.captures.size())
                project_.captures.erase(project_.captures.begin() + rows[i]);
        projectChanged(true);
    };
    audition_.onClick = [this] {
        const int r = assignTable_.getSelectedRow();
        if (r < 0 || r >= (int) project_.captures.size())
            return;
        const auto& c = project_.captures[(size_t) r];
        auto err = ctx_.engine.loadReference(toFile(c.path));
        if (err.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "A/B", err);
            return;
        }
        // put the parametric model's controls where the capture was made, if it is this project's model
        if (const auto* info = ctx_.engine.modelInfo(); info && info->knobs.size() == project_.knobs.size())
            for (size_t k = 0; k < c.knobs01.size(); ++k)
                ctx_.engine.setKnob((int) k, c.knobs01[k]);
        ctx_.modelChanged.sendChangeMessage();
    };

    // plan tab
    planTab_.addAndMakeVisible(planText_);
    planTab_.addAndMakeVisible(refreshPlan_);
    planText_.setMultiLine(true);
    planText_.setReadOnly(true);
    planText_.setFont(uiFont(14.0f));
    refreshPlan_.onClick = [this] { updatePlan(); };

    // train tab
    for (auto* c : std::initializer_list<juce::Component*> { &arch_, &steps_, &seconds_, &knobScale_, &addDi_, &clearDi_, &train_, &cancel_, &play_, &archLabel_,
                                                             &stepsLabel_, &secondsLabel_, &diLabel_, &status_, &verdict_, &progressBar_, &log_, &results_,
                                                             &versions_ })
        trainTab_.addAndMakeVisible(c);
    arch_.addItem("Standard  (16/8 ch, best quality)", 1);
    arch_.addItem("Lite  (12/6 ch)", 2);
    arch_.addItem("Feather  (8/4 ch, lowest CPU)", 3);
    arch_.onChange = [this] {
        const char* a[] = { "standard", "lite", "feather" };
        project_.architecture = a[juce::jlimit(0, 2, arch_.getSelectedItemIndex())];
    };
    for (auto* s : { &steps_, &seconds_ })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    }
    steps_.setRange(500, 40000, 500);
    steps_.setNumDecimalPlacesToDisplay(0);
    seconds_.setNumDecimalPlacesToDisplay(0);
    seconds_.setRange(20, 600, 10);
    seconds_.setTextValueSuffix(" s");
    steps_.onValueChange = [this] { project_.steps = (int) steps_.getValue(); };
    seconds_.onValueChange = [this] { project_.signalSeconds = seconds_.getValue(); };
    knobScale_.onClick = [this] { project_.knobScale = knobScale_.getToggleState(); };
    archLabel_.setText("Network", juce::dontSendNotification);
    stepsLabel_.setText("Training steps", juce::dontSendNotification);
    secondsLabel_.setText("Training signal", juce::dontSendNotification);
    for (auto* l : { &archLabel_, &stepsLabel_, &secondsLabel_ })
        styleLabel(*l, 12.0f, true, colours::textDim);
    styleLabel(diLabel_, 12.0f, false, colours::textDim);
    styleLabel(status_, 12.5f, false, colours::text);
    styleLabel(verdict_, 14.0f, true, colours::text);
    verdict_.setJustificationType(juce::Justification::topLeft);
    addDi_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Add DI guitar recordings (WAV, at the captures' sample rate)", juce::File(), "*.wav");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this](const juce::FileChooser& fc) {
                                  for (auto& f : fc.getResults())
                                      project_.diFiles.push_back(toPath(f));
                                  projectChanged(false);
                              });
    };
    clearDi_.onClick = [this] {
        project_.diFiles.clear();
        projectChanged(false);
    };
    train_.onClick = [this] { startTraining(); };
    cancel_.onClick = [this] { cancel_flag_.store(true); };
    play_.onClick = [this] {
        if (project_.versions.empty())
            return;
        ctx_.engine.loadModel(toFile(project_.versions.back().file), [this](juce::String err) {
            if (err.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Cannot load", err);
            ctx_.modelChanged.sendChangeMessage();
        });
    };
    log_.setMultiLine(true);
    log_.setReadOnly(true);
    log_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    results_.setModel(resultsModel_.get());
    results_.setRowHeight(24);
    auto& rh = results_.getHeader();
    rh.addColumn("Capture", 1, 150, 50, 400, juce::TableHeaderComponent::notSortable);
    rh.addColumn("Setting", 2, 230, 50, 600, juce::TableHeaderComponent::notSortable);
    rh.addColumn("Role", 3, 120, 50, 200, juce::TableHeaderComponent::notSortable);
    rh.addColumn("ESR", 4, 70, 50, 120, juce::TableHeaderComponent::notSortable);
    rh.addColumn("Verdict", 5, 80, 50, 120, juce::TableHeaderComponent::notSortable);
    rh.addColumn("Nearest capture ESR (snapshot switching)", 6, 260, 50, 500, juce::TableHeaderComponent::notSortable);
    versions_.setModel(versionsModel_.get());
    versions_.setRowHeight(26);
    cancel_.setEnabled(false);

    refreshProjectList();
    auto last = ctx_.settings->getValue("lastProject");
    auto existing = ctx_.projectsDir().findChildFiles(juce::File::findFiles, false, "*.pnamproj");
    existing.sort();
    if (last.isNotEmpty() && juce::File(last).existsAsFile())
        openProject(juce::File(last));
    else if (!existing.isEmpty())
        openProject(existing[0]);
    else
        projectChanged(true);
    startTimerHz(2);
}

StudioPage::~StudioPage()
{
    cancel_flag_.store(true);
    if (worker_ && worker_->joinable())
        worker_->join();
}

void StudioPage::refreshProjectList()
{
    projectBox_.clear(juce::dontSendNotification);
    auto files = ctx_.projectsDir().findChildFiles(juce::File::findFiles, false, "*.pnamproj");
    files.sort();
    for (int i = 0; i < files.size(); ++i)
        projectBox_.addItem(files[i].getFileNameWithoutExtension(), i + 1);
    for (int i = 0; i < files.size(); ++i)
        if (files[i] == toFile(project_.file))
            projectBox_.setSelectedItemIndex(i, juce::dontSendNotification);
    if (project_.file.empty())
        projectBox_.setText("No project - click New", juce::dontSendNotification);
}

void StudioPage::newProject(const juce::String& amp)
{
    project_ = StudioProject();
    project_.ampName = amp.toStdString();
    project_.file = toPath(ctx_.projectsDir().getChildFile(juce::File::createLegalFileName(amp) + ".pnamproj").getNonexistentSibling());
    for (const char* n : { "Gain", "Bass", "Middle", "Treble" })
    {
        KnobInfo k;
        k.name = n;
        project_.knobs.push_back(k);
    }
    // captures already grouped under this amp name come in automatically
    std::vector<const CaptureEntry*> mine = ctx_.captures.capturesOfAmp(project_.ampName);
    addCapturesToProject(mine);
    saveProject();
    projectChanged(true);
}

void StudioPage::openProject(const juce::File& f)
{
    try
    {
        project_ = StudioProject::load(toPath(f));
        ctx_.settings->setValue("lastProject", f.getFullPathName());
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project", e.what());
    }
    static_cast<VersionsModel*>(versionsModel_.get())->project = &project_;
    lastReport_ = {};
    if (!project_.versions.empty())
        try
        {
            auto j = readJsonFile(project_.versions.back().file);
            const auto r = j["metadata"]["training"]["report"];
            for (const auto& c : r.value("captures", nlohmann::json::array()))
            {
                CaptureReport cr;
                cr.label = c.value("label", std::string());
                cr.knobs01 = c.value("knobs", std::vector<float>());
                cr.holdout = c.value("holdout", false);
                cr.esr = c.value("esr", -1.0);
                cr.nearestLabel = c.value("nearest", std::string());
                cr.nearestEsr = c.value("nearest_esr", -1.0);
                lastReport_.captures.push_back(cr);
            }
            lastReport_.verdict = r.value("verdict", std::string());
        }
        catch (...)
        {
        }
    projectChanged(true);
    refreshProjectList();
    showResults(lastReport_);
}

void StudioPage::saveProject()
{
    if (project_.file.empty())
        return;
    try
    {
        project_.save();
        ctx_.settings->setValue("lastProject", toFile(project_.file).getFullPathName());
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save project", e.what());
    }
    refreshProjectList();
}

void StudioPage::addCapturesToProject(const std::vector<const CaptureEntry*>& caps)
{
    if (project_.file.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Capture Studio", "Create a project first (New).");
        return;
    }
    for (auto* e : caps)
    {
        if (std::any_of(project_.captures.begin(), project_.captures.end(), [&](const ProjectCapture& c) { return c.path == e->path; }))
            continue;
        ProjectCapture pc;
        pc.path = e->path;
        pc.label = e->info.name.empty() ? toUtf8(e->path.stem()) : e->info.name;
        for (auto& k : project_.knobs)
            pc.knobs01.push_back(k.defaultValue);
        project_.captures.push_back(pc);
        if (e->amp.empty() && !project_.ampName.empty())
            ctx_.captures.setAmp(e->path, project_.ampName);
    }
    ctx_.captures.save();
    captures_.refresh();
    projectChanged(true);
}

void StudioPage::projectChanged(bool structure)
{
    ampName_.setText(project_.ampName, juce::dontSendNotification);
    manufacturer_.setText(project_.manufacturer, juce::dontSendNotification);
    const char* archs[] = { "standard", "lite", "feather" };
    for (int i = 0; i < 3; ++i)
        if (project_.architecture == archs[i])
            arch_.setSelectedItemIndex(i, juce::dontSendNotification);
    steps_.setValue(project_.steps, juce::dontSendNotification);
    seconds_.setValue(project_.signalSeconds, juce::dontSendNotification);
    knobScale_.setToggleState(project_.knobScale, juce::dontSendNotification);
    diLabel_.setText(project_.diFiles.empty() ? "Built-in guitar-like signal only. Adding your own DI playing is recommended."
                                              : juce::String(project_.diFiles.size()) + " DI recording(s) added to the built-in signal.",
                     juce::dontSendNotification);
    if (structure)
        rebuildAssignColumns();
    controlsTable_.updateContent();
    assignTable_.updateContent();
    assignTable_.repaint();
    controlsTable_.repaint();
    versions_.updateContent();
    updatePlan();
    if (!project_.file.empty())
        try
        {
            project_.save();
        }
        catch (...)
        {
        }
    for (auto* c : std::initializer_list<juce::Component*> { &tabs_, &ampName_, &manufacturer_, &saveProject_ })
        c->setEnabled(!project_.file.empty());
}

void StudioPage::rebuildAssignColumns()
{
    auto& h = assignTable_.getHeader();
    h.removeAllColumns();
    h.addColumn("Capture", 1, 180, 60, 400, juce::TableHeaderComponent::notSortable);
    for (size_t k = 0; k < project_.knobs.size(); ++k)
        h.addColumn(project_.knobs[k].name, 100 + (int) k, 74, 40, 200, juce::TableHeaderComponent::notSortable);
    h.addColumn("Hold out", 800, 70, 50, 100, juce::TableHeaderComponent::notSortable);
    h.addColumn("Used for", 900, 100, 60, 200, juce::TableHeaderComponent::notSortable);
}

void StudioPage::updatePlan()
{
    std::vector<std::vector<float>> caps;
    int unassigned = 0, held = 0;
    for (auto& c : project_.captures)
    {
        if (!c.assigned)
            ++unassigned;
        else if (c.holdout)
            ++held;
        else
            caps.push_back(c.knobs01);
    }
    plan_ = analyseCapturePlan(project_.knobs, caps);
    juce::String t;
    t << "CAPTURE PLAN FOR " << juce::String(project_.ampName).toUpperCase() << "\n\n";
    t << (int) caps.size() << " training captures, " << held << " hold-out (validation) captures";
    if (unassigned)
        t << ", " << unassigned << " captures whose control positions are not entered yet";
    t << ".  About " << plan_.recommendedTotal << " training captures are recommended for " << (int) project_.knobs.size() << " controls.\n\n";
    if (held == 0)
        t << "!  No hold-out captures: the model can be trained, but it cannot be validated on settings it has not seen. "
             "Mark one or more captures as \"Hold out\".\n\n";
    const char* sev[] = { "-  ", "!  ", "X  " };
    for (auto& f : plan_.findings)
        t << sev[(int) f.severity] << juce::String(f.text) << "\n";
    if (!plan_.suggestions.empty())
    {
        t << "\nSUGGESTED NEXT CAPTURES\n";
        int i = 1;
        for (auto& s : plan_.suggestions)
            t << juce::String(i++).paddedLeft(' ', 2) << ".  " << juce::String(describeSetting(project_.knobs, s.knobs01)) << "     (" << juce::String(s.reason) << ")\n";
    }
    t << "\n" << (plan_.trainable ? "Ready to train." : "Not ready to train: fix the items marked X.");
    planText_.setText(t, false);
}

void StudioPage::startTraining()
{
    if (training_.load())
        return;
    TrainingConfig cfg;
    cfg.knobs = project_.knobs;
    int cabs = 0, nocabs = 0;
    for (auto& c : project_.captures)
    {
        if (!c.assigned)
            continue;
        CaptureSpec s;
        s.path = c.path;
        s.knobs01 = c.knobs01;
        s.holdout = c.holdout;
        s.label = c.label;
        cfg.captures.push_back(s);
        if (auto* e = ctx_.captures.find(c.path))
            (e->cab == CaptureEntry::Cab::yes || e->cab == CaptureEntry::Cab::likely ? cabs : nocabs)++;
    }
    cfg.architecture = project_.architecture;
    cfg.steps = project_.steps;
    cfg.knobScale = project_.knobScale;
    cfg.signal.seconds = project_.signalSeconds;
    cfg.signal.userDiFiles = project_.diFiles;
    cfg.metadata = { { "name", project_.ampName }, { "gear_make", project_.manufacturer }, { "gear_model", project_.ampName },
                     { "modeled_by", project_.creator }, { "gear_type", cabs > nocabs ? "amp_cab" : "amp" } };
    auto problems = checkTrainingConfig(cfg);
    if (!problems.empty())
    {
        juce::StringArray a;
        for (auto& p : problems)
            a.add(p);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Not ready to train", a.joinIntoString("\n\n"));
        return;
    }
    saveProject();
    training_.store(true);
    cancel_flag_.store(false);
    train_.setEnabled(false);
    cancel_.setEnabled(true);
    log_.clear();
    progress_ = 0;
    tabs_.setCurrentTabIndex(2);
    if (worker_ && worker_->joinable())
        worker_->join();
    juce::Component::SafePointer<StudioPage> safe(this);
    worker_ = std::make_unique<std::thread>([this, cfg, safe] {
        TrainingResult res;
        juce::String err;
        try
        {
            res = trainParametric(cfg, [safe](const TrainingProgress& p) {
                juce::MessageManager::callAsync([safe, p] {
                    if (!safe)
                        return;
                    auto& s = *safe;
                    if (p.stage == "rendering")
                        s.progress_ = 0.05 * p.step / juce::jmax(1, p.totalSteps);
                    else if (p.stage == "training")
                        s.progress_ = 0.05 + 0.9 * p.step / juce::jmax(1, p.totalSteps);
                    else
                        s.progress_ = 0.97;
                    juce::String line;
                    if (p.stage == "training")
                    {
                        s.status_.setText("Training: step " + juce::String(p.step) + " / " + juce::String(p.totalSteps) + "   loss " + juce::String(p.loss, 4),
                                          juce::dontSendNotification);
                        if (p.valEsr >= 0)
                            line << "step " << p.step << "  validation ESR " << juce::String(p.valEsr, 4)
                                 << (p.holdoutEsr >= 0 ? "   held-out ESR " + juce::String(p.holdoutEsr, 4) : juce::String()) << "\n";
                    }
                    else
                    {
                        s.status_.setText(p.message, juce::dontSendNotification);
                        line << "[" << p.stage << "] " << p.message << "\n";
                    }
                    if (line.isNotEmpty())
                    {
                        s.log_.moveCaretToEnd();
                        s.log_.insertTextAtCaret(line);
                    }
                });
            }, &cancel_flag_);
        }
        catch (const std::exception& e)
        {
            err = e.what();
        }
        juce::MessageManager::callAsync([safe, res, err]() mutable {
            if (safe)
                safe->trainingFinished(std::move(res), err);
        });
    });
}

void StudioPage::trainingFinished(TrainingResult res, juce::String error)
{
    training_.store(false);
    train_.setEnabled(true);
    cancel_.setEnabled(false);
    progress_ = error.isEmpty() ? 1.0 : 0.0;
    if (error.isNotEmpty())
    {
        status_.setText(error == "cancelled" ? "Training cancelled." : "Training failed.", juce::dontSendNotification);
        if (error != "cancelled")
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Training failed", error);
        return;
    }
    // version management: <Projects>/<amp> models/<amp> vN.nam
    const int number = project_.versions.empty() ? 1 : project_.versions.back().number + 1;
    auto dir = toFile(project_.file).getSiblingFile(toFile(project_.file).getFileNameWithoutExtension() + " models");
    dir.createDirectory();
    auto file = dir.getChildFile(juce::File::createLegalFileName(juce::String(project_.ampName) + " v" + juce::String(number)) + ".nam");
    try
    {
        writeJsonFile(toPath(file), res.model);
        ModelVersion v;
        v.number = number;
        v.file = toPath(file);
        v.date = todayString();
        v.architecture = project_.architecture;
        v.trainedEsr = res.report.meanTrainedEsr;
        v.holdoutEsr = res.report.meanHoldoutEsr;
        v.baselineEsr = res.report.meanNearestBaselineEsr;
        v.verdict = res.report.validatedOnUnseenSettings ? "unseen settings " + esrVerdict(v.holdoutEsr) : "not validated (no hold-outs)";
        project_.versions.push_back(v);
        saveProject();
        auto& entry = ctx_.models.addModel(v.file, true);
        entry.manufacturer = project_.manufacturer;
        entry.amp = project_.ampName;
        entry.category = project_.category;
        entry.validation = v.verdict;
        ctx_.models.save();
        ctx_.libraryChanged.sendChangeMessage();
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Saving the model failed", e.what());
    }
    lastReport_ = res.report;
    showResults(lastReport_);
    versions_.updateContent();
    status_.setText("Done in " + juce::String(res.report.seconds / 60.0, 1) + " min. Saved as " + file.getFileName() + " and added to the library.",
                    juce::dontSendNotification);
}

void StudioPage::showResults(const TrainingReport& r)
{
    juce::String v = r.verdict;
    for (auto& w : r.warnings)
        v << "\n" << juce::String(w);
    verdict_.setText(v.isEmpty() ? juce::String("No model trained yet.") : v, juce::dontSendNotification);
    results_.updateContent();
    results_.repaint();
}

void StudioPage::timerCallback() { play_.setEnabled(!project_.versions.empty() && !training_.load()); }

void StudioPage::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    drawPanel(g, tabs_.getBounds().getUnion(ampLabel_.getBounds()).expanded(10).toFloat(), "Parametric model project");
}

void StudioPage::resized()
{
    auto r = getLocalBounds().reduced(18);
    captures_.setBounds(r.removeFromLeft(juce::jmax(420, r.getWidth() * 36 / 100)));
    r.removeFromLeft(18);
    r.reduce(10, 10);
    r.removeFromTop(20);
    auto head = r.removeFromTop(48);
    auto pb = head.removeFromLeft(240);
    projectBox_.setBounds(pb.removeFromBottom(28));
    head.removeFromLeft(8);
    newProject_.setBounds(head.removeFromLeft(60).removeFromBottom(28));
    head.removeFromLeft(6);
    saveProject_.setBounds(head.removeFromLeft(60).removeFromBottom(28));
    head.removeFromLeft(14);
    auto a = head.removeFromLeft(head.getWidth() / 2);
    ampLabel_.setBounds(a.removeFromTop(18));
    ampName_.setBounds(a.reduced(0, 1));
    head.removeFromLeft(8);
    makerLabel_.setBounds(head.removeFromTop(18));
    manufacturer_.setBounds(head.reduced(0, 1));
    r.removeFromTop(10);
    tabs_.setBounds(r);

    // controls tab
    {
        auto t = controlsTab_.getLocalBounds().reduced(4, 10);
        controlsTitle_.setBounds(t.removeFromTop(20));
        auto cb = t.removeFromTop(26 * 5 + 30);
        auto cbtn = cb.removeFromRight(110);
        addControl_.setBounds(cbtn.removeFromTop(30));
        cbtn.removeFromTop(6);
        removeControl_.setBounds(cbtn.removeFromTop(30));
        controlsTable_.setBounds(cb.withTrimmedRight(10));
        t.removeFromTop(10);
        assignTitle_.setBounds(t.removeFromTop(36));
        auto btns = t.removeFromBottom(32);
        removeCapture_.setBounds(btns.removeFromLeft(140));
        btns.removeFromLeft(8);
        audition_.setBounds(btns.removeFromLeft(180));
        t.removeFromBottom(6);
        assignTable_.setBounds(t);
    }
    // plan tab
    {
        auto t = planTab_.getLocalBounds().reduced(4, 10);
        refreshPlan_.setBounds(t.removeFromBottom(30).removeFromLeft(120));
        t.removeFromBottom(6);
        planText_.setBounds(t);
    }
    // train tab
    {
        auto t = trainTab_.getLocalBounds().reduced(4, 10);
        auto settings = t.removeFromTop(120);
        auto col1 = settings.removeFromLeft(settings.getWidth() / 2).withTrimmedRight(12);
        archLabel_.setBounds(col1.removeFromTop(18));
        arch_.setBounds(col1.removeFromTop(28));
        col1.removeFromTop(6);
        stepsLabel_.setBounds(col1.removeFromTop(18));
        steps_.setBounds(col1.removeFromTop(28));
        secondsLabel_.setBounds(settings.removeFromTop(18));
        seconds_.setBounds(settings.removeFromTop(28));
        settings.removeFromTop(6);
        knobScale_.setBounds(settings.removeFromTop(24));
        auto di = settings.removeFromTop(28);
        addDi_.setBounds(di.removeFromLeft(150));
        di.removeFromLeft(6);
        clearDi_.setBounds(di.removeFromLeft(60));
        diLabel_.setBounds(di.withTrimmedLeft(8));
        t.removeFromTop(8);
        auto run = t.removeFromTop(32);
        train_.setBounds(run.removeFromLeft(200));
        run.removeFromLeft(8);
        cancel_.setBounds(run.removeFromLeft(80));
        run.removeFromLeft(8);
        play_.setBounds(run.removeFromRight(160));
        progressBar_.setBounds(run.reduced(4, 6));
        status_.setBounds(t.removeFromTop(24));
        auto bottom = t.removeFromBottom(t.getHeight() / 2);
        auto logArea = t.removeFromLeft(t.getWidth() / 2).withTrimmedRight(8);
        log_.setBounds(logArea);
        verdict_.setBounds(t.removeFromTop(64));
        versions_.setBounds(t);
        results_.setBounds(bottom.withTrimmedTop(8));
    }
}
} // namespace pnam::ui
