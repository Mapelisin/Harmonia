#include "app/MainWindow.h"
#include "app/SelfTest.h"
#include "audio/DeviceSetup.h"
#include "audio/Limiter.h"
#include "audio/Pump.h"
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

        std::unique_ptr<core::Session> createSession (bool openDevices)
        {
            core::Session::Options options;
            options.openDevices = openDevices;
            options.uiBehaviour = std::make_unique<ui::HarmoniaUIBehaviour>();
            options.scanPluginsOutOfProcess = true;
            auto session = std::make_unique<core::Session> (std::move (options));
            audio::limiter::registerType (session->getEngine());
            audio::pump::registerType (session->getEngine());
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
            return isPluginScanChild (commandLine) || SelfTestOptions::fromCommandLine (commandLine).has_value();
        }

        void initialise (const juce::String& commandLine) override
        {
            if (te::PluginManager::startChildProcessPluginScan (commandLine))
                return;

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

            mainWindow = std::make_unique<MainWindow> (*session, MainWindow::createNewProject (*session));

            // Work a crash left behind comes first; else a project file given on the command line
            // (double-clicked in Explorer).
            mainWindow->offerToRestore ([this, commandLine]
            {
                if (const juce::File file (commandLine.unquoted().trim()); commandLine.isNotEmpty()
                        && juce::File::isAbsolutePath (file.getFullPathName()) && file.existsAsFile()
                        && file.hasFileExtension (MainWindow::fileExtension))
                    mainWindow->open (file);
            });

            if (deviceProblem.isNotEmpty())
                mainWindow->showWarning (deviceProblem);
        }

        void shutdown() override
        {
            mainWindow.reset();
            session.reset();
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
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
    };
}

START_JUCE_APPLICATION (harmonia::app::HarmoniaApplication)
