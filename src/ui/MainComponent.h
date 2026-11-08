#pragma once

#include "core/Project.h"
#include "ui/StatusBar.h"

namespace harmonia::ui
{
    /** Root of the main window: header (project info, audio settings), workspace, status bar. */
    class MainComponent final : public juce::Component
    {
    public:
        MainComponent (core::Session&, core::Project&);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        core::Session& session;
        core::Project& project;

        juce::TextButton audioSettingsButton { "Audio settings" };
        StatusBar statusBar { session };

        void showAudioSettings();
        juce::String getProjectSummary() const;
    };
}
