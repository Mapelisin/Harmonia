#include "MainComponent.h"
#include "PluginWindow.h"
#include "SelfTest.h"

namespace harmonia::spike
{
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (te::Engine& engine)
            : juce::DocumentWindow ("Harmonia - Spike 01", juce::Colour (0xff1e1f22), juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine), true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    class SpikeApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override          { return true; }

        void initialise (const juce::String&) override
        {
            engine = std::make_unique<te::Engine> ("HarmoniaSpike01", std::make_unique<SpikeUIBehaviour>(), nullptr);

            const auto args = getCommandLineParameterArray();

            if (args.contains ("--selftest"))
            {
                juce::MessageManager::callAsync ([this, options = parseSelfTestOptions (args)]
                {
                    setApplicationReturnValue (runSelfTest (*engine, options));
                    quit();
                });
                return;
            }

            mainWindow = std::make_unique<MainWindow> (*engine);
        }

        void shutdown() override
        {
            mainWindow.reset();
            engine.reset();
        }

    private:
        std::unique_ptr<te::Engine> engine;
        std::unique_ptr<MainWindow> mainWindow;

        static SelfTestOptions parseSelfTestOptions (const juce::StringArray& args)
        {
            const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

            SelfTestOptions options;
            options.logFile = exeDir.getChildFile ("selftest.log");
            options.renderDirectory = exeDir.getChildFile ("renders");

            for (auto& arg : args)
            {
                const auto value = arg.fromFirstOccurrenceOf ("=", false, false).unquoted();

                if (arg.startsWith ("--log="))
                    options.logFile = juce::File (value);
                else if (arg.startsWith ("--plugin="))
                    options.pluginPath = value;
                else if (arg == "--no-device")
                    options.testLiveDevice = false;
            }

            return options;
        }
    };
}

START_JUCE_APPLICATION (harmonia::spike::SpikeApplication)
