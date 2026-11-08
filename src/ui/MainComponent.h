#pragma once

#include "audio/MidiInputs.h"
#include "audio/NotePreview.h"
#include "audio/PlaybackClock.h"
#include "audio/Transport.h"
#include "core/Project.h"
#include "ui/FileCommands.h"
#include "ui/InstrumentPanel.h"
#include "ui/NoteInput.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"
#include "ui/arrangement/Arrangement.h"
#include "ui/mixer/Mixer.h"
#include "ui/pianoroll/PianoRoll.h"

namespace harmonia::ui
{
    /** Root of the main window: header (file menu, transport, audio settings), the current
        track's instrument, the arrangement and below it (a draggable divider between them)
        the piano roll of the open clip or the mixer (F7 / F9), and the status bar. */
    class MainComponent final : public juce::Component,
                                private pianoroll::PianoRollModel::Listener,
                                private arrangement::ArrangementModel::Listener
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

        /** Shows for a few seconds in the status bar that something worked (e.g. "Saved"). */
        void showMessage (const juce::String&);

        /** New / Open / Save / Export, provided by the window (none: those shortcuts do nothing). */
        void setFileCommands (FileCommands*);

        pianoroll::PianoRollModel& getPianoRollModel() noexcept         { return pianoRollModel; }
        arrangement::ArrangementModel& getArrangementModel() noexcept   { return arrangementModel; }
        arrangement::Arrangement& getArrangement() noexcept             { return arrangement; }
        pianoroll::PianoRoll& getPianoRoll() noexcept                   { return pianoRoll; }
        NoteInput& getNoteInput() noexcept                              { return noteInput; }
        audio::Transport& getTransport() noexcept                       { return transport; }
        mixer::Mixer& getMixer() noexcept                               { return mixer; }

        /** What the lower part of the window shows. */
        enum class LowerPanel { pianoRoll, mixer };
        void showLowerPanel (LowerPanel);
        LowerPanel getLowerPanel() const noexcept                       { return lowerPanel; }

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
        arrangement::ArrangementModel arrangementModel { project };
        audio::PlaybackClock playbackClock { project.getEdit() };
        audio::MidiInputs midiInputs { project.getEdit() };
        NoteInput noteInput { pianoRollModel, notePreview, playbackClock };
        FileCommands* fileCommands = nullptr;

        juce::ReferenceCountedObjectPtr<te::AudioTrack> currentTrack;
        core::note_ops::BeatRange playedLoop { -1.0, -1.0 };

        TransportBar transportBar { transport, project.getEdit(), noteInput, arrangementModel };
        juce::TextButton fileButton { "File" };
        juce::TextButton audioSettingsButton { "Audio settings" };
        juce::Label trackNameLabel;
        juce::TextButton pianoRollTab { "Piano roll" }, mixerTab { "Mixer" };
        std::unique_ptr<InstrumentPanel> instrumentPanel;
        arrangement::Arrangement arrangement { arrangementModel, transport };
        juce::StretchableLayoutManager splitLayout;
        juce::StretchableLayoutResizerBar splitter { &splitLayout, 1, false };
        pianoroll::PianoRoll pianoRoll { pianoRollModel, transport };
        mixer::Mixer mixer { session, arrangementModel };
        LowerPanel lowerPanel = LowerPanel::pianoRoll;
        StatusBar statusBar { session };

        /** The arrangement's current track or open clip changed: the piano roll, the keyboards
            and the instrument row follow. */
        void currentChanged();
        void updateLoop();
        void updateMidiStatus();
        void toggleMetronome();
        void togglePlayMode();
        void playFrom (double beat);

        void showFileMenu();
        void showAudioSettings();
        juce::String getProjectSummary() const;

        void pianoRollChanged() override;
        void arrangementChanged() override;

        JUCE_DECLARE_NON_COPYABLE (MainComponent)
    };
}
