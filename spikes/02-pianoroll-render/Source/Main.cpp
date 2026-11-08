#include "Benchmark.h"

namespace harmonia::spike
{
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const NoteSet& notes, bool runBenchmark, bool driveFromTimer, bool useOpenGL, juce::File reportFile)
            : juce::DocumentWindow ("Harmonia - Spike 02 (piano roll render)", juce::Colours::black,
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            view.setNotes (&notes);
            view.setSize (1600, 900);
            setContentNonOwned (&view, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);

            if (useOpenGL)
                openGLContext.attachTo (view);

            if (runBenchmark)
                benchmark = std::make_unique<Benchmark> (view, reportFile, driveFromTimer, useOpenGL, []
                {
                    juce::JUCEApplication::getInstance()->quit();
                });
        }

        ~MainWindow() override
        {
            benchmark.reset();
            openGLContext.detach();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        juce::OpenGLContext openGLContext;
        PianoRollView view;
        std::unique_ptr<Benchmark> benchmark;
    };

    class SpikeApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override          { return true; }

        void initialise (const juce::String&) override
        {
            const auto args = getCommandLineParameterArray();
            const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

            notes = NoteSet::generate (20000, 256, 1234);
            const bool useTimer = args.contains ("--timer");
            const bool useOpenGL = args.contains ("--opengl");
            const auto logName = juce::String ("benchmark-") + (useOpenGL ? "opengl-" : "") + (useTimer ? "timer" : "vblank") + ".log";
            mainWindow = std::make_unique<MainWindow> (notes, args.contains ("--bench"), useTimer, useOpenGL,
                                                       exeDir.getChildFile (logName));
        }

        void shutdown() override { mainWindow.reset(); }

    private:
        NoteSet notes;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

START_JUCE_APPLICATION (harmonia::spike::SpikeApplication)
