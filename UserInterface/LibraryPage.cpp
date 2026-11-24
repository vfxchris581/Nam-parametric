#include "LibraryPage.h"

#include <set>

#include "Snapshot.h"
#include "Validation.h"

namespace pnam::ui
{
namespace
{
enum Cols { colFav = 1, colName, colAmp, colMaker, colCategory, colControls, colRate, colValidation };

juce::String joinStrings(const std::vector<std::string>& v)
{
    juce::StringArray a;
    for (auto& s : v)
        a.add(s);
    return a.joinIntoString(", ");
}
std::vector<std::string> splitStrings(const juce::String& s)
{
    std::vector<std::string> v;
    for (auto& t : juce::StringArray::fromTokens(s, ",", ""))
        if (t.trim().isNotEmpty())
            v.push_back(t.trim().toStdString());
    return v;
}
juce::String validationOf(const ModelEntry& e)
{
    if (!e.validation.empty())
        return e.validation;
    try
    {
        auto j = readJsonFile(e.path);
        const auto t = j.value("metadata", nlohmann::json::object()).value("training", nlohmann::json::object());
        if (t.contains("report"))
        {
            const auto& r = t["report"];
            if (r.value("validated_on_unseen_settings", false))
                return juce::String(esrVerdict(r.value("mean_holdout_esr", -1.0))) + " (unseen)";
            return "fit " + juce::String(esrVerdict(r.value("mean_trained_esr", -1.0))) + ", not validated";
        }
    }
    catch (...)
    {
    }
    return "unknown";
}
} // namespace

LibraryPage::LibraryPage(AppContext& ctx) : ctx_(ctx)
{
    filterModel_.page = this;
    filterList_.setModel(&filterModel_);
    filterList_.setRowHeight(28);
    table_.setModel(this);
    table_.setRowHeight(30);
    auto& h = table_.getHeader();
    h.addColumn("", colFav, 30, 30, 30, juce::TableHeaderComponent::notSortable);
    h.addColumn("Model", colName, 220);
    h.addColumn("Amplifier", colAmp, 140);
    h.addColumn("Manufacturer", colMaker, 120);
    h.addColumn("Category", colCategory, 100);
    h.addColumn("Controls", colControls, 200);
    h.addColumn("Rate", colRate, 60);
    h.addColumn("Validation", colValidation, 170);
    h.setSortColumnId(colName, true);
    search_.setTextToShowWhenEmpty("Search models, amps, tags...", colours::textDim);
    search_.onTextChange = [this] { refresh(); };
    for (auto* c : std::initializer_list<juce::Component*> { &filterList_, &table_, &search_, &import_, &load_, &remove_, &snapshot_, &save_, &title_,
                                                             &info_, &name_, &manufacturer_, &amp_, &channel_, &creator_, &cabinet_, &tags_,
                                                             &collections_, &description_, &category_ })
        addAndMakeVisible(c);
    description_.setMultiLine(true, true);
    description_.setReturnKeyStartsNewLine(true);
    for (auto& c : ModelLibrary::standardCategories())
        category_.addItem(c, category_.getNumItems() + 1);
    category_.setEditableText(true);
    styleLabel(title_, 18.0f, true);
    styleLabel(info_, 12.0f, false, colours::textDim);
    info_.setJustificationType(juce::Justification::topLeft);
    const char* names[] = { "Name", "Manufacturer", "Amplifier", "Channel", "Category", "Creator", "Cabinet", "Tags", "Collections", "Description" };
    for (auto* n : names)
    {
        auto* l = fieldLabels_.add(new juce::Label({}, n));
        styleLabel(*l, 11.5f, true, colours::textDim);
        addAndMakeVisible(l);
    }
    import_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import Parametric NAM models", juce::File(), "*.nam");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this](const juce::FileChooser& fc) { importModels(fc.getResults()); });
    };
    load_.onClick = [this] {
        if (auto* e = selected())
            ctx_.engine.loadModel(toFile(e->path), [this](juce::String err) {
                if (err.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Cannot load model", err);
                else
                    ctx_.modelChanged.sendChangeMessage();
            });
    };
    remove_.onClick = [this] {
        if (auto* e = selected())
        {
            const auto p = e->path;
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Remove model",
                                               "Remove " + juce::String(e->name) + " from the library and delete its file from the library folder?",
                                               "Remove", "Cancel", nullptr, juce::ModalCallbackFunction::create([this, p](int r) {
                                                   if (r == 1)
                                                   {
                                                       ctx_.models.remove(p, true);
                                                       ctx_.models.save();
                                                       refresh();
                                                   }
                                               }));
        }
    };
    snapshot_.onClick = [this] {
        auto* e = selected();
        if (!e || toFile(e->path) != ctx_.engine.modelFile())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export plain NAM",
                                                   "Play the model first (the export uses the current control positions).");
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>("Export plain NAM capture", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                                                    .getChildFile(juce::String(e->name) + " snapshot.nam"), "*.nam");
        const auto src = e->path;
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, src](const juce::FileChooser& fc) {
                                  auto f = fc.getResult();
                                  if (f == juce::File())
                                      return;
                                  try
                                  {
                                      auto j = readJsonFile(src);
                                      std::vector<float> g;
                                      for (size_t i = 0; i < j["config"]["knobs"].size(); ++i)
                                          g.push_back(ctx_.engine.knob((int) i));
                                      writeJsonFile(toPath(f), snapshotToPlain(j, g));
                                  }
                                  catch (const std::exception& ex)
                                  {
                                      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Export failed", ex.what());
                                  }
                              });
    };
    save_.onClick = [this] { saveDetails(); };
    ctx_.libraryChanged.addChangeListener(this);
    refresh();
}

LibraryPage::~LibraryPage() { ctx_.libraryChanged.removeChangeListener(this); }

void LibraryPage::importModels(const juce::Array<juce::File>& files)
{
    juce::StringArray problems;
    for (auto& f : files)
    {
        try
        {
            ctx_.models.addModel(toPath(f));
        }
        catch (const std::exception& e)
        {
            problems.add(f.getFileName() + ": " + e.what());
        }
    }
    ctx_.models.save();
    refresh();
    if (!problems.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Some files were not added", problems.joinIntoString("\n\n"));
}

bool LibraryPage::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase(".nam"))
            return true;
    return false;
}

void LibraryPage::filesDropped(const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> fs;
    for (auto& f : files)
        fs.add(juce::File(f));
    importModels(fs);
}

void LibraryPage::refresh()
{
    // filters: all, favourites, categories present, collections present
    filters_.clear();
    filters_.push_back({ "All models", [](const ModelEntry&) { return true; } });
    filters_.push_back({ "Favorites", [](const ModelEntry& e) { return e.favorite; } });
    std::set<std::string> cats(ModelLibrary::standardCategories().begin(), ModelLibrary::standardCategories().end()), cols;
    for (auto& e : ctx_.models.entries())
    {
        if (!e.category.empty())
            cats.insert(e.category);
        for (auto& c : e.collections)
            cols.insert(c);
    }
    for (auto& c : cats)
        filters_.push_back({ c, [c](const ModelEntry& e) { return e.category == c; } });
    for (auto& c : cols)
        filters_.push_back({ juce::String::fromUTF8("\xe2\x96\xb8 ") + juce::String(c), [c](const ModelEntry& e) {
                                return std::find(e.collections.begin(), e.collections.end(), c) != e.collections.end();
                            } });
    filter_ = juce::jlimit(0, (int) filters_.size() - 1, filter_);
    filterList_.updateContent();
    filterList_.selectRow(filter_, true, true);

    const auto q = search_.getText().toLowerCase();
    rows_.clear();
    for (auto& e : ctx_.models.entries())
    {
        if (!filters_[(size_t) filter_].second(e))
            continue;
        const juce::String hay = (e.name + " " + e.amp + " " + e.manufacturer + " " + e.category + " " + e.description + " "
                                  + joinStrings(e.tags).toStdString())
                                     .c_str();
        if (q.isNotEmpty() && !hay.toLowerCase().contains(q))
            continue;
        rows_.push_back(&e);
    }
    auto key = [this](const ModelEntry* e) -> juce::String {
        switch (sortCol_)
        {
            case colAmp: return e->amp;
            case colMaker: return e->manufacturer;
            case colCategory: return e->category;
            case colRate: return juce::String(e->info.sampleRate);
            default: return e->name;
        }
    };
    std::stable_sort(rows_.begin(), rows_.end(), [&](auto* a, auto* b) {
        const int c = key(a).compareNatural(key(b));
        return sortForward_ ? c < 0 : c > 0;
    });
    table_.updateContent();
    table_.repaint();
    showDetails();
}

void LibraryPage::selectModel(const juce::File& f)
{
    for (size_t i = 0; i < rows_.size(); ++i)
        if (toFile(rows_[i]->path) == f)
            table_.selectRow((int) i);
}

ModelEntry* LibraryPage::selected()
{
    const int r = table_.getSelectedRow();
    return r >= 0 && r < (int) rows_.size() ? rows_[(size_t) r] : nullptr;
}

void LibraryPage::showDetails()
{
    auto* e = selected();
    for (auto* c : std::initializer_list<juce::Component*> { &name_, &manufacturer_, &amp_, &channel_, &creator_, &cabinet_, &tags_, &collections_,
                                                             &description_, &category_, &save_, &load_, &remove_, &snapshot_ })
        c->setEnabled(e != nullptr);
    if (!e)
    {
        title_.setText(ctx_.models.entries().empty() ? "Your library is empty" : "Select a model", juce::dontSendNotification);
        info_.setText(ctx_.models.entries().empty()
                          ? "Import Parametric NAM models (.nam with architecture ParametricWaveNet), or build one from your NAM captures "
                            "in the Capture Studio. Ordinary single-setting NAM captures are training material, not library models."
                          : "",
                      juce::dontSendNotification);
        return;
    }
    title_.setText(e->name, juce::dontSendNotification);
    juce::StringArray lines;
    juce::StringArray ctrls;
    for (auto& k : e->info.knobs)
        ctrls.add(juce::String(k.name) + (k.isSwitch() ? " (switch)" : ""));
    lines.add("Controls: " + ctrls.joinIntoString(", "));
    lines.add(juce::String(e->info.configSummary));
    lines.add("Sample rate " + juce::String(e->info.sampleRate, 0) + " Hz" + (e->info.hasKnobScale ? "  ·  knob scaling (spec 1.1)" : ""));
    lines.add("Validation: " + validationOf(*e));
    lines.add(juce::String(toUtf8(e->path)));
    info_.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
    name_.setText(e->name, false);
    manufacturer_.setText(e->manufacturer, false);
    amp_.setText(e->amp, false);
    channel_.setText(e->channel, false);
    creator_.setText(e->creator, false);
    cabinet_.setText(e->cabinet, false);
    tags_.setText(joinStrings(e->tags), false);
    collections_.setText(joinStrings(e->collections), false);
    description_.setText(e->description, false);
    category_.setText(e->category, juce::dontSendNotification);
}

void LibraryPage::saveDetails()
{
    auto* e = selected();
    if (!e)
        return;
    e->name = name_.getText().toStdString();
    e->manufacturer = manufacturer_.getText().toStdString();
    e->amp = amp_.getText().toStdString();
    e->channel = channel_.getText().toStdString();
    e->creator = creator_.getText().toStdString();
    e->cabinet = cabinet_.getText().toStdString();
    e->tags = splitStrings(tags_.getText());
    e->collections = splitStrings(collections_.getText());
    e->description = description_.getText().toStdString();
    e->category = category_.getText().toStdString();
    ctx_.models.save();
    const auto keep = e->path;
    refresh();
    selectModel(toFile(keep));
}

void LibraryPage::paintRowBackground(juce::Graphics& g, int row, int, int, bool sel)
{
    g.fillAll(sel ? colours::accentDim.withAlpha(0.55f) : (row % 2 ? colours::panel : colours::panel.brighter(0.02f)));
}

void LibraryPage::paintCell(juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row >= (int) rows_.size())
        return;
    const auto& e = *rows_[(size_t) row];
    juce::String t;
    g.setColour(colours::text);
    g.setFont(uiFont(13.0f, col == colName));
    switch (col)
    {
        case colFav:
            g.setColour(e.favorite ? colours::accent : colours::outline.brighter(0.3f));
            g.setFont(uiFont(16.0f));
            g.drawText(e.favorite ? juce::String::fromUTF8("\xe2\x98\x85") : juce::String::fromUTF8("\xe2\x98\x86"), 0, 0, w, h, juce::Justification::centred);
            return;
        case colName: t = e.name; break;
        case colAmp: t = e.amp; break;
        case colMaker: t = e.manufacturer; break;
        case colCategory: t = e.category; break;
        case colControls:
        {
            juce::StringArray a;
            for (auto& k : e.info.knobs)
                a.add(k.name);
            t = a.joinIntoString(" · ");
            g.setColour(colours::textDim);
            break;
        }
        case colRate: t = juce::String(e.info.sampleRate / 1000.0, 1) + "k"; break;
        case colValidation:
        {
            t = validationOf(e);
            g.setColour(t.startsWith("excellent") || t.startsWith("good") ? colours::good : (t.contains("not") || t == "unknown" ? colours::textDim : colours::warn));
            break;
        }
        default: break;
    }
    g.drawText(t, 8, 0, w - 12, h, juce::Justification::centredLeft, true);
}

void LibraryPage::cellClicked(int row, int col, const juce::MouseEvent&)
{
    if (col == colFav && row < (int) rows_.size())
    {
        rows_[(size_t) row]->favorite = !rows_[(size_t) row]->favorite;
        ctx_.models.save();
        table_.repaintRow(row);
    }
}

void LibraryPage::cellDoubleClicked(int, int, const juce::MouseEvent&) { load_.onClick(); }
void LibraryPage::selectedRowsChanged(int) { showDetails(); }
void LibraryPage::sortOrderChanged(int col, bool fwd)
{
    sortCol_ = col;
    sortForward_ = fwd;
    refresh();
}

int LibraryPage::FilterModel::getNumRows() { return (int) page->filters_.size(); }
void LibraryPage::FilterModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (row >= (int) page->filters_.size())
        return;
    if (sel)
    {
        g.setColour(colours::accentDim.withAlpha(0.5f));
        g.fillRoundedRectangle(2.0f, 1.0f, (float) w - 4, (float) h - 2, 5.0f);
    }
    int count = 0;
    for (auto& e : page->ctx_.models.entries())
        count += page->filters_[(size_t) row].second(e) ? 1 : 0;
    g.setColour(sel ? juce::Colours::white : colours::text);
    g.setFont(uiFont(13.0f, row < 2));
    g.drawText(page->filters_[(size_t) row].first, 12, 0, w - 50, h, juce::Justification::centredLeft);
    g.setColour(colours::textDim);
    g.drawText(juce::String(count), w - 40, 0, 30, h, juce::Justification::centredRight);
}
void LibraryPage::FilterModel::selectedRowsChanged(int r)
{
    if (r >= 0 && r != page->filter_)
    {
        page->filter_ = r;
        page->refresh();
    }
}

void LibraryPage::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    drawPanel(g, filterList_.getBounds().expanded(8).withTrimmedTop(-20).toFloat(), "Browse");
    drawPanel(g, title_.getBounds().getUnion(description_.getBounds()).getUnion(save_.getBounds()).expanded(12).toFloat());
}

void LibraryPage::resized()
{
    auto r = getLocalBounds().reduced(18);
    auto left = r.removeFromLeft(200);
    left.removeFromTop(28);
    filterList_.setBounds(left.reduced(8));
    r.removeFromLeft(16);
    auto right = r.removeFromRight(340).reduced(12);
    r.removeFromRight(16);
    auto bar = r.removeFromTop(34);
    import_.setBounds(bar.removeFromRight(150));
    bar.removeFromRight(10);
    search_.setBounds(bar);
    r.removeFromTop(10);
    auto actions = r.removeFromBottom(34);
    load_.setBounds(actions.removeFromLeft(90));
    actions.removeFromLeft(8);
    remove_.setBounds(actions.removeFromLeft(90));
    actions.removeFromLeft(8);
    snapshot_.setBounds(actions.removeFromLeft(280));
    r.removeFromBottom(10);
    table_.setBounds(r);

    title_.setBounds(right.removeFromTop(28));
    info_.setBounds(right.removeFromTop(92));
    save_.setBounds(right.removeFromBottom(30));
    right.removeFromBottom(8);
    juce::Component* fields[] = { &name_, &manufacturer_, &amp_, &channel_, &category_, &creator_, &cabinet_, &tags_, &collections_, &description_ };
    for (int i = 0; i < 10; ++i)
    {
        fieldLabels_[i]->setBounds(right.removeFromTop(16));
        fields[i]->setBounds(i == 9 ? right : right.removeFromTop(26));
        right.removeFromTop(4);
    }
}
} // namespace pnam::ui
