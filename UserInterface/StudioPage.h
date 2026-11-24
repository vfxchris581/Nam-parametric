#pragma once
// The Parametric Model Creation Studio: import NAM captures, group them by amplifier, define the
// amplifier's controls, tell the studio at which control positions each capture was made,
// check what is missing, train one parametric model, validate it, version it and play it.
#include "AppContext.h"
#include "CapturePlanner.h"
#include "Trainer.h"
#include "Widgets.h"

namespace pnam::ui
{
class StudioPage;

// left: every imported source capture
class CaptureTable : public juce::Component, private juce::TableListBoxModel, public juce::FileDragAndDropTarget
{
public:
    explicit CaptureTable(StudioPage& page, AppContext& ctx);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;
    std::vector<const CaptureEntry*> selection() const;

private:
    int getNumRows() override { return (int) rows_.size(); }
    void paintRowBackground(juce::Graphics&, int, int, int, bool) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool) override;
    void selectedRowsChanged(int) override;
    void importPaths(const juce::StringArray& paths);
    StudioPage& page_;
    AppContext& ctx_;
    std::vector<const CaptureEntry*> rows_;
    juce::TableListBox table_;
    juce::ComboBox ampFilter_;
    juce::TextButton importFiles_ { "Import captures..." }, importFolder_ { "Import folder..." }, setAmp_ { "Set amplifier..." },
        addToProject_ { "Add to project  >" }, remove_ { "Remove" };
    juce::Label hint_, groupProblems_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

class StudioPage : public juce::Component, private juce::Timer
{
public:
    explicit StudioPage(AppContext& ctx);
    ~StudioPage() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    void addCapturesToProject(const std::vector<const CaptureEntry*>& caps);
    void openProject(const juce::File& f);
    void newProject(const juce::String& ampName);
    StudioProject& project() { return project_; }
    void projectChanged(bool structure);   // controls or captures changed
    void startTraining();
    bool isTraining() const { return training_.load(); }
    void runPlanForScreenshot() { updatePlan(); }
    void showTab(int i) { tabs_.setCurrentTabIndex(i); }

private:
    friend class CaptureTable;
    struct ControlsModel;
    struct AssignModel;
    struct ResultsModel;
    void timerCallback() override;
    void updatePlan();
    void saveProject();
    void showResults(const TrainingReport& r);
    void rebuildAssignColumns();
    void refreshProjectList();
    void trainingFinished(TrainingResult res, juce::String error);

    AppContext& ctx_;
    StudioProject project_;
    CaptureTable captures_;

    // project header
    juce::ComboBox projectBox_;
    juce::TextButton newProject_ { "New" }, saveProject_ { "Save" };
    juce::TextEditor ampName_, manufacturer_;
    juce::Label ampLabel_, makerLabel_;

    // tabs
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    juce::Component controlsTab_, planTab_, trainTab_;

    // controls & captures
    std::unique_ptr<ControlsModel> controlsModel_;
    std::unique_ptr<AssignModel> assignModel_;
    juce::TableListBox controlsTable_, assignTable_;
    juce::TextButton addControl_ { "+ Control" }, removeControl_ { "- Control" }, removeCapture_ { "Remove capture" }, audition_ { "A/B selected capture" };
    juce::Label controlsTitle_, assignTitle_;

    // plan
    juce::TextEditor planText_;
    juce::TextButton refreshPlan_ { "Re-check" };

    // training
    juce::ComboBox arch_;
    juce::Slider steps_, seconds_;
    juce::ToggleButton knobScale_ { "Knob scaling (FiLM, spec 1.1)" };
    juce::TextButton addDi_ { "Add DI recordings..." }, clearDi_ { "Clear" }, train_ { "Train parametric model" }, cancel_ { "Cancel" },
        play_ { "Play latest version" };
    juce::Label archLabel_, stepsLabel_, secondsLabel_, diLabel_, status_, verdict_;
    double progress_ = 0;
    juce::ProgressBar progressBar_ { progress_ };
    juce::TextEditor log_;
    std::unique_ptr<ResultsModel> resultsModel_;
    juce::TableListBox results_;
    juce::ListBox versions_;
    std::unique_ptr<juce::ListBoxModel> versionsModel_;

    std::unique_ptr<juce::FileChooser> chooser_;
    std::atomic<bool> training_ { false }, cancel_flag_ { false };
    std::unique_ptr<std::thread> worker_;
    TrainingReport lastReport_;
    PlanResult plan_;
};
} // namespace pnam::ui
