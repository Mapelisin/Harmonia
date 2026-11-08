#include "app/MainWindow.h"
#include "ui/Theme.h"

namespace harmonia::app
{
    class HarmoniaApplication final : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return "Harmonia"; }
        const juce::String getApplicationVersion() override { return HARMONIA_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override          { return false; }

        void initialise (const juce::String&) override
        {
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            session = std::make_unique<core::Session> (core::Session::Options {});
            project = core::Project::createEmpty (*session);
            mainWindow = std::make_unique<MainWindow> (*session, *project);
        }

        void shutdown() override
        {
            mainWindow.reset();
            project.reset();
            session.reset();
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        void systemRequestedQuit() override
        {
            quit();
        }

    private:
        ui::theme::LookAndFeel lookAndFeel;
        std::unique_ptr<core::Session> session;
        std::unique_ptr<core::Project> project;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

START_JUCE_APPLICATION (harmonia::app::HarmoniaApplication)
