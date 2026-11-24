#pragma once
// The model library: only Parametric NAM models are listed as playable amps.
#include "AppContext.h"
#include "Widgets.h"

namespace pnam::ui
{
class LibraryPage : public juce::Component,
                    private juce::TableListBoxModel,
                    private juce::ChangeListener,
                    public juce::FileDragAndDropTarget
{
public:
    explicit LibraryPage(AppContext& ctx);
    ~LibraryPage() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void selectModel(const juce::File& f);

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    // table
    int getNumRows() override { return (int) rows_.size(); }
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked(int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked(int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged(int) override;
    void sortOrderChanged(int col, bool forwards) override;
    // filter list (ListBoxModel; getNumRows clashes, so use a nested model)
    struct FilterModel : juce::ListBoxModel
    {
        LibraryPage* page = nullptr;
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged(int) override;
    } filterModel_;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    ModelEntry* selected();
    void showDetails();
    void saveDetails();
    void importModels(const juce::Array<juce::File>& files);

    AppContext& ctx_;
    std::vector<ModelEntry*> rows_;
    std::vector<std::pair<juce::String, std::function<bool(const ModelEntry&)>>> filters_;
    int filter_ = 0, sortCol_ = 2;
    bool sortForward_ = true;
    juce::ListBox filterList_;
    juce::TableListBox table_;
    juce::TextEditor search_;
    juce::TextButton import_ { "Import models..." }, load_ { "Play" }, remove_ { "Remove" }, snapshot_ { "Export plain NAM at current settings..." },
        save_ { "Save details" };
    juce::Label title_, info_;
    juce::TextEditor name_, manufacturer_, amp_, channel_, creator_, cabinet_, tags_, collections_, description_;
    juce::ComboBox category_;
    juce::OwnedArray<juce::Label> fieldLabels_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
} // namespace pnam::ui
