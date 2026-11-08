#pragma once

#include "audio/NotePreview.h"
#include "audio/Transport.h"
#include "core/Project.h"
#include "ui/InstrumentPanel.h"
#include "ui/PreviewKeyboard.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"

namespace harmonia::ui
{
    /** Root of the main window: header (project, transport, audio settings), the first track's
        instrument, an on-screen keyboard and the status bar. */
    class MainComponent final : public juce::Component
    {
    public:
        MainComponent (core::Session&, core::Project&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        /** Shows a problem in the status bar (e.g. the chosen audio interface didn't start). */
        void showWarning (const juce::String&);

    private:
        core::Session& session;
        core::Project& project;

        audio::Transport transport { project.getEdit() };
        audio::NotePreview notePreview;

        TransportBar transportBar { transport, project.getEdit() };
        juce::TextButton audioSettingsButton { "Audio settings" };
        std::unique_ptr<InstrumentPanel> instrumentPanel;
        PreviewKeyboard keyboard { notePreview };
        StatusBar statusBar { session };

        void showAudioSettings();
        juce::String getProjectSummary() const;

        JUCE_DECLARE_NON_COPYABLE (MainComponent)
    };
}
