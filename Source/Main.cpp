// Parametric NAM Studio - application entry point.
//
//   ParametricNAMStudio                       normal start
//   ParametricNAMStudio --screenshots DIR     render every page to PNG (used for documentation/CI), then quit
#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"

namespace
{
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name)
        : DocumentWindow(name, pnam::ui::colours::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* mc = new pnam::ui::MainComponent();
        setContentOwned(mc, true);
        if (auto xml = mc->context().settings->getXmlValue("audioDevice"))
            mc->context().engine.initialiseAudio(xml.get());
        else
            mc->context().engine.initialiseAudio(nullptr);
        setResizable(true, true);
        setResizeLimits(1100, 720, 4096, 2400);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

void takeScreenshots(const juce::File& dir)
{
    dir.createDirectory();
    auto mc = std::make_unique<pnam::ui::MainComponent>();
    mc->setSize(1440, 900);
    auto& ctx = mc->context();
    // Demo content (Build/make_demo_content.py): recall the first preset, else play the newest model.
    try
    {
        auto presets = ctx.presets.list();
        if (!presets.empty())
        {
            auto s = pnam::PresetState::load(presets.front());
            ctx.engine.loadModelBlocking(juce::File(juce::String::fromUTF8(s.modelPath.c_str())));
            auto problems = ctx.engine.applyState(s);
            if (problems.isNotEmpty())
                std::fprintf(stderr, "screenshot preset: %s\n", problems.toRawUTF8());
            mc->header().setCurrentPreset(pnam::ui::toFile(presets.front()));
        }
        else if (!ctx.models.entries().empty())
            ctx.engine.loadModelBlocking(pnam::ui::toFile(ctx.models.entries().back().path));
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "screenshot setup: %s\n", e.what());
    }
    ctx.modelChanged.sendSynchronousChangeMessage();
    mc->playPage().syncFromEngine();
    const char* names[] = { "1-amp", "2-library", "3-capture-studio", "4-settings" };
    for (int t = 0; t < 4; ++t)
    {
        mc->tabs().setCurrentTabIndex(t);
        if (t == 1)
            mc->libraryPage().selectModel(ctx.engine.modelFile());
        mc->resized();
        auto img = mc->createComponentSnapshot(mc->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out(dir.getChildFile(juce::String(names[t]) + ".png"));
        out.setPosition(0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream(img, out);
    }
    // the studio's other tabs
    auto& studio = mc->studioPage();
    mc->tabs().setCurrentTabIndex(2);
    for (int t = 1; t < 3; ++t)
    {
        studio.runPlanForScreenshot();
        studio.showTab(t);
        mc->resized();
        auto img = mc->createComponentSnapshot(mc->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out(dir.getChildFile(juce::String(t == 1 ? "3b-capture-plan" : "3c-train-validate") + ".png"));
        out.setPosition(0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream(img, out);
    }
}
} // namespace

class ParametricNamStudioApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Parametric NAM Studio"; }
    const juce::String getApplicationVersion() override { return PNAM_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& cmd) override
    {
        auto args = juce::StringArray::fromTokens(cmd, true);
        const int i = args.indexOf("--screenshots");
        if (i >= 0 && i + 1 < args.size())
        {
            takeScreenshots(juce::File::getCurrentWorkingDirectory().getChildFile(args[i + 1].unquoted()));
            quit();
            return;
        }
        window_ = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { window_.reset(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow> window_;
};

START_JUCE_APPLICATION(ParametricNamStudioApp)
