#include "app/MainWindow.h"
#include "app/SelfTest.h"
#include "audio/DeviceSetup.h"
#include "audio/BuiltInPlugins.h"
#include "core/ProjectFiles.h"
#include "sandbox/Host.h"
#include "sandbox/Platform.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"

namespace harmonia::app
{
    namespace
    {
        /** Harmonia.exe is also started as the plugin-scanning child process. */
        bool isPluginScanChild (const juce::String& commandLine)
        {
            return commandLine.contains ("PluginScan");
        }

        /** ...and as the process each plugin runs in. */
        std::optional<sandbox::Host::Options> getPluginHostOptions (const juce::String& commandLine)
        {
            return sandbox::Host::fromCommandLine (juce::StringArray::fromTokens (commandLine, true));
        }

        std::unique_ptr<core::Session> createSession (bool openDevices)
        {
            core::Session::Options options;
            options.openDevices = openDevices;
            options.uiBehaviour = std::make_unique<ui::HarmoniaUIBehaviour>();
            options.scanPluginsOutOfProcess = true;
            options.pluginHost = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            auto session = std::make_unique<core::Session> (std::move (options));
            audio::registerPluginTypes (session->getEngine());
            return session;
        }
    }

    class HarmoniaApplication final : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return "Harmonia"; }
        const juce::String getApplicationVersion() override { return HARMONIA_VERSION_STRING; }

        bool moreThanOneInstanceAllowed() override
        {
            const auto commandLine = getCommandLineParameters();
            return isPluginScanChild (commandLine) || getPluginHostOptions (commandLine).has_value()
                    || SelfTestOptions::fromCommandLine (commandLine).has_value();
        }

        void initialise (const juce::String& commandLine) override
        {
            if (te::PluginManager::startChildProcessPluginScan (commandLine))
                return;

            if (auto hostOptions = getPluginHostOptions (commandLine))
            {
                sandbox::platform::prepareHostProcess();
                hostOptions->addFormats = [] (juce::AudioPluginFormatManager& formats) { juce::addDefaultFormatsToManager (formats); };
                hostOptions->onFinished = [] { juce::JUCEApplication::quit(); };
                pluginHost = std::make_unique<sandbox::Host> (std::move (*hostOptions));
                return;
            }

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            if (auto selfTest = SelfTestOptions::fromCommandLine (commandLine))
            {
                session = createSession (selfTest->useDevice);

                juce::MessageManager::callAsync ([this, options = *selfTest]
                {
                    setApplicationReturnValue (runSelfTest (*session, options));
                    quit();
                });

                return;
            }

            session = createSession (true);
            const auto deviceProblem = audio::checkStartupDevice (*session);

            // The song Harmonia starts with: the user's own template, if one is chosen for that.
            juce::String templateProblem;
            mainWindow = std::make_unique<MainWindow> (*session, MainWindow::createStartProject (*session, &templateProblem));

            if (templateProblem.isNotEmpty())
                mainWindow->showWarning (templateProblem);

            // Work a crash left behind comes first; else a project file given on the command line
            // (double-clicked in Explorer).
            mainWindow->offerToRestore ([this, commandLine]
            {
                if (const auto file = core::project_files::fromCommandLine (commandLine); file != juce::File())
                    mainWindow->open (file);
            });

            if (deviceProblem.isNotEmpty())
                mainWindow->showWarning (deviceProblem);
        }

        void shutdown() override
        {
            pluginHost.reset();
            mainWindow.reset();
            session.reset();
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        /** A project double-clicked while Harmonia is open: JUCE hands its command line here (the
            second Harmonia quits), and it opens in this window after asking about unsaved changes. */
        void anotherInstanceStarted (const juce::String& commandLine) override
        {
            if (mainWindow == nullptr)
                return;

            if (mainWindow->isMinimised())
                mainWindow->setMinimised (false);

            mainWindow->toFront (true);

            if (const auto file = core::project_files::fromCommandLine (commandLine); file != juce::File())
                mainWindow->openRecentProject (file);
        }

        void systemRequestedQuit() override
        {
            if (mainWindow != nullptr)
                mainWindow->askToSaveChanges ([this] { quit(); });
            else
                quit();
        }

    private:
        ui::theme::LookAndFeel lookAndFeel;
        std::unique_ptr<core::Session> session;
        std::unique_ptr<MainWindow> mainWindow;
        std::unique_ptr<sandbox::Host> pluginHost;
    };
}

START_JUCE_APPLICATION (harmonia::app::HarmoniaApplication)
