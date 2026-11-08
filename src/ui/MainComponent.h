#pragma once

#include "audio/MidiInputs.h"
#include "audio/NotePreview.h"
#include "audio/PlaybackClock.h"
#include "audio/Transport.h"
#include "core/Project.h"
#include "ui/InstrumentPanel.h"
#include "ui/NoteInput.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"
#include "ui/pianoroll/PianoRoll.h"

namespace harmonia::ui
{
    /** Root of the main window: header (project, transport, audio settings), the track row
        (which track the piano roll edits, its instrument), the piano roll and the status bar. */
    class MainComponent final : public juce::Component,
                                private pianoroll::PianoRollModel::Listener,
                                private juce::ValueTree::Listener,
                                private juce::AsyncUpdater
    {
    public:
        MainComponent (core::Session&, core::Project&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool isKeyDown) override;

        /** Shows a problem in the status bar (e.g. the chosen audio interface didn't start). */
        void showWarning (const juce::String&);

        pianoroll::PianoRollModel& getPianoRollModel() noexcept     { return pianoRollModel; }
        NoteInput& getNoteInput() noexcept                          { return noteInput; }

    private:
        /** Plays edited notes on the current track's instrument, but not over the playing song. */
        struct PreviewAuditioner final : pianoroll::Auditioner
        {
            PreviewAuditioner (audio::NotePreview& p, audio::Transport& t) : preview (p), transport (t) {}

            void noteOn (int pitch, int velocity) override
            {
                if (! transport.isPlaying())
                    preview.noteOn (pitch, velocity);
            }

            void noteOff (int pitch) override       { preview.noteOff (pitch); }

            audio::NotePreview& preview;
            audio::Transport& transport;
        };

        core::Session& session;
        core::Project& project;

        audio::Transport transport { project.getEdit() };
        audio::NotePreview notePreview;
        PreviewAuditioner auditioner { notePreview, transport };
        pianoroll::PianoRollModel pianoRollModel { project };
        audio::PlaybackClock playbackClock { project.getEdit() };
        audio::MidiInputs midiInputs { project.getEdit() };
        NoteInput noteInput { pianoRollModel, notePreview, playbackClock };

        juce::ReferenceCountedObjectPtr<te::AudioTrack> currentTrack;

        TransportBar transportBar { transport, project.getEdit(), noteInput };
        juce::TextButton audioSettingsButton { "Audio settings" };
        juce::Label trackLabel;
        juce::ComboBox trackBox;
        juce::TextButton addTrackButton { "Add track" };
        std::unique_ptr<InstrumentPanel> instrumentPanel;
        pianoroll::PianoRoll pianoRoll { pianoRollModel, transport };
        StatusBar statusBar { session };

        void selectTrack (te::AudioTrack*);
        void addTrack();
        void refreshTrackList();
        void updateLoop();
        void updateMidiStatus();
        void toggleMetronome();

        void showAudioSettings();
        juce::String getProjectSummary() const;

        void pianoRollChanged() override;
        void handleAsyncUpdate() override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

        JUCE_DECLARE_NON_COPYABLE (MainComponent)
    };
}
