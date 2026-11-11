#pragma once

#include "audio/AudioInputs.h"
#include "audio/AutomationWriter.h"
#include "audio/MidiLearn.h"
#include "audio/MixCheck.h"
#include "audio/FilePreview.h"
#include "audio/MidiInputs.h"
#include "audio/NotePreview.h"
#include "audio/PluginSafety.h"
#include "audio/PlaybackClock.h"
#include "audio/Transport.h"
#include "core/Project.h"
#include "ui/CpuMeter.h"
#include "ui/ExportProgress.h"
#include "ui/FileCommands.h"
#include "ui/InstrumentPanel.h"
#include "ui/MainMenu.h"
#include "ui/NoteInput.h"
#include "ui/ShortcutsPanel.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"
#include "ui/arrangement/Arrangement.h"
#include "ui/audioeditor/AudioEditor.h"
#include "ui/automation/AutomationPanel.h"
#include "ui/browser/SoundBrowser.h"
#include "ui/mixer/Mixer.h"
#include "ui/pianoroll/PianoRoll.h"
#include "ui/stepseq/StepSequencer.h"

namespace harmonia::ui
{
    /** Root of the main window: the menu bar (with the audio load), the transport, the current
        track's instrument, the arrangement and below it (a draggable divider between them)
        the sound browser, the editor (the piano roll of the open clip, or on an audio track the
        audio clip editor), the automation of the current track or the mixer (F6 / F7 / F8 / F9),
        and the status bar. */
    class MainComponent final : public juce::Component,
                                public juce::DragAndDropContainer,
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

        /** File > Collect all files into the song's folder: the sounds the song uses from elsewhere
            are copied next to it and used from there (core::song_files::collect). Says what was
            done; one undo step. */
        void collectFiles();

        /** File > Find missing files: says which sounds aren't where the song has them, and asks
            for a folder to look for them in. */
        void findMissingFiles();

        /** Looks for the missing sounds in a folder and the folders inside it, and says how many
            were found. One undo step. */
        void lookForMissingFilesIn (const juce::File& folder);

        /** The missing sounds as the question lists them: a line each, with where it was and
            what uses it. */
        juce::String describeMissingFiles() const;

        pianoroll::PianoRollModel& getPianoRollModel() noexcept         { return pianoRollModel; }
        StatusBar& getStatusBar() noexcept                              { return statusBar; }
        arrangement::ArrangementModel& getArrangementModel() noexcept   { return arrangementModel; }

        /** File -> Check loudness: renders the song, measures it the way streaming services do and
            says how to bring it to their level (offering to change the master volume). */
        void checkLoudness();

        /** Adds `gainDb` to the master volume (one undo step) and says what changed. */
        void applyLoudnessGain (float gainDb);

        /** Brings the song to the streaming level with the limiter on the master (one undo step). */
        void raiseWithLimiter();

        /** File -> Check the mix: renders the song and each track, then shows Mix Doctor's report
            (clipping, mono, bass and top, tracks lost under the others), with a button to fix the
            levels if that is what's wrong. */
        void checkMix();

        /** Mix Doctor's fixes (one undo step), saying what changed. */
        void fixMix (const audio::mix_check::Fixes&);

        /** "bars 5-8", "bar 3", or in beats when it isn't whole bars ("beats 2.50-5"): for names.
            Bars and beats are counted from 1. */
        static juce::String describeBars (core::note_ops::BeatRange, double beatsPerBar);
        arrangement::Arrangement& getArrangement() noexcept             { return arrangement; }
        pianoroll::PianoRoll& getPianoRoll() noexcept                   { return pianoRoll; }
        audioeditor::AudioEditor& getAudioEditor() noexcept             { return audioEditor; }
        stepseq::StepSequencer& getStepSequencer() noexcept             { return stepSequencer; }

        /** The step sequencer in place of the piano roll (the same clip, as drums and steps), or
            the piano roll back (Alt+T, the Steps and Piano roll buttons, the View menu). */
        void setStepsShown (bool);
        bool areStepsShown() const noexcept                             { return stepsShown; }
        NoteInput& getNoteInput() noexcept                              { return noteInput; }
        audio::MidiInputs& getMidiInputs() noexcept                     { return midiInputs; }
        audio::Transport& getTransport() noexcept                       { return transport; }
        TransportBar& getTransportBar() noexcept                        { return transportBar; }
        mixer::Mixer& getMixer() noexcept                               { return mixer; }
        automation::AutomationPanel& getAutomationPanel() noexcept      { return automationPanel; }
        audio::AudioInputs& getAudioInputs() noexcept                   { return audioInputs; }
        browser::SoundBrowser& getSoundBrowser() noexcept               { return soundBrowser; }
        audio::FilePreview& getFilePreview() noexcept                   { return filePreview; }

        /** Puts sound or MIDI files into the song at the playhead: onto the current track if it
            suits them, else onto new tracks. Says in the status bar what happened. */
        void addFilesAtPlayhead (const juce::Array<juce::File>&);

        /** The list of every shortcut over the window (F1 or the Shortcuts button). */
        void showShortcuts (bool shouldShow);
        bool areShortcutsShown() const                                  { return shortcuts.isVisible(); }
        ShortcutsPanel& getShortcutsPanel() noexcept                    { return shortcuts; }

        /** The menu bar's File menu. */
        juce::PopupMenu createFileMenu();

        MainMenu& getMainMenu() noexcept                                { return mainMenu; }
        juce::MenuBarComponent& getMenuBar() noexcept                   { return menuBar; }
        CpuMeter& getCpuMeter() noexcept                                { return cpuMeter; }

        //==============================================================================
        // Commands of the menus and the keys.

        /** Undo / redo, wherever the keyboard is (Ctrl+Z / Ctrl+Y). */
        void undo();
        void redo();

        /** Whether Edit's commands (Ctrl+C, Ctrl+V...) go to the clips of the arrangement rather
            than to the notes of the piano roll: while the arrangement has the keyboard, or the
            piano roll is hidden. */
        bool isArrangementTheEditTarget() const;

        /** The playhead back to where Play starts from: the song's start, or the open clip's (Home). */
        void goToStart();

        /** Play the song or the open clip in a loop (L switches). */
        void setPlayMode (arrangement::PlayMode);
        void togglePlayMode();

        /** Plays from the next / previous marker (Ctrl+Right / Ctrl+Left); from the start before the first. */
        void jumpToMarker (bool next);

        void toggleMetronome();

        /** For a sound's menu in the sound browser: while the current track plays Harmonia's drum
            machine, "Use as a drum sound" with its drums (the one chosen plays the file). */
        void addDrumSoundItems (juce::PopupMenu&, const juce::File&);

        /** The sound as an instrument: a new sampler track that plays it, or (the current track
            plays the sampler) the sound of that one. */
        void addSamplerItems (juce::PopupMenu&, const juce::File&);
        void addSamplerTrack (const juce::File&);
        void useInSampler (const juce::File&);

        /** The file becomes the sound of that key of the current track's drum machine. */
        void useAsDrumSound (const juce::File&, int note);

        /** Writing automation on / off (Ctrl+Shift+A, the Write button). */
        void toggleAutomationWriting();
        audio::automation::Writer& getAutomationWriter() noexcept       { return automationWriter; }

        /** MIDI Learn for whatever is moved next, on / off (Ctrl+Shift+L, Edit menu). */
        void toggleMidiLearn();
        audio::midi_learn::Controller& getMidiControls() noexcept       { return midiControls; }

        /** File > Settings (Ctrl+,): the sound card, MIDI keyboards, plugins. */
        void showSettings();

        /** Help → About: the version, and what Harmonia is made with. */
        void showAbout();

        /** Closes every plugin window of the song (F12, View menu) and says how many. */
        void closePluginWindows();

        /** Which plugin takes how much of the sound's time: a bubble at the CPU meter. */
        void showPluginLoad();

        /** Runs an export with its progress over the whole window (ui::ExportProgress): playback
            stops, the window stays alive and Cancel or Esc stops the export. `onDone` hears of
            its end, well or not, unless it was cancelled. */
        void runExport (std::unique_ptr<audio::ExportJob>, std::function<void (audio::ExportJob&)> onDone);
        bool isExporting() const noexcept                               { return exportProgress.isRunning(); }
        ExportProgress& getExportProgress() noexcept                    { return exportProgress; }

        /** What the lower part of the window shows. `pianoRoll` is the editor of the current
            track: the piano roll, or on an audio track the audio clip editor. */
        enum class LowerPanel { pianoRoll, automation, mixer, sounds };
        void showLowerPanel (LowerPanel);
        LowerPanel getLowerPanel() const noexcept                       { return lowerPanel; }

        /** The current track is an audio track: its editor (F7) is the audio clip editor. */
        bool editsAudio() const;

        /** The name of the editor panel: "Piano roll" or "Audio clip". */
        juce::String getEditorName() const;

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
        audio::FilePreview filePreview { session };
        PreviewAuditioner auditioner { notePreview, transport };
        pianoroll::PianoRollModel pianoRollModel { project };
        arrangement::ArrangementModel arrangementModel { project };
        audio::PlaybackClock playbackClock { project.getEdit() };
        audio::MidiInputs midiInputs { project.getEdit() };
        audio::AudioInputs audioInputs { project.getEdit() };
        NoteInput noteInput { pianoRollModel, notePreview, playbackClock };
        audio::midi_learn::Controller midiControls { project.getEdit() };
        audio::automation::Writer automationWriter { project.getEdit() };
        FileCommands* fileCommands = nullptr;

        juce::ReferenceCountedObjectPtr<te::AudioTrack> currentTrack;
        core::note_ops::BeatRange playedLoop { -1.0, -1.0 };

        TransportBar transportBar { transport, project.getEdit(), noteInput, arrangementModel, automationWriter };
        MainMenu mainMenu { *this };
        juce::MenuBarComponent menuBar { &mainMenu };
        CpuMeter cpuMeter;
        juce::TextButton shortcutsButton { "?" };
        juce::Label trackNameLabel;
        juce::TextButton soundsTab { "Sounds" }, pianoRollTab { "Piano roll" }, automationTab { "Automation" }, mixerTab { "Mixer" };
        std::unique_ptr<InstrumentPanel> instrumentPanel;
        arrangement::Arrangement arrangement { arrangementModel, transport };
        juce::StretchableLayoutManager splitLayout;
        juce::StretchableLayoutResizerBar splitter { &splitLayout, 1, false };
        pianoroll::PianoRoll pianoRoll { pianoRollModel, transport };
        audioeditor::AudioEditor audioEditor { arrangementModel, transport };
        stepseq::StepSequencer stepSequencer { pianoRollModel, transport };
        bool stepsShown = false;
        mixer::Mixer mixer { session, arrangementModel };
        automation::AutomationPanel automationPanel { arrangementModel, arrangement.getView(), transport };
        browser::SoundBrowser soundBrowser { session, filePreview };
        LowerPanel lowerPanel = LowerPanel::pianoRoll;
        StatusBar statusBar { session };
        ShortcutsPanel shortcuts;
        ExportProgress exportProgress;
        std::unique_ptr<juce::FileChooser> importChooser;

        // A plugin's process crashed (a warning; the mixer marks its slot) or came back.
        audio::plugin_safety::Watcher pluginWatcher { session, [this] (te::Plugin& plugin) { pluginStatusChanged (plugin); } };
        void pluginStatusChanged (te::Plugin&);

        /** The piano roll or the audio clip editor, as the current track needs. */
        void updateEditor();

        /** The arrangement's current track or open clip changed: the piano roll, the keyboards
            and the instrument row follow. */
        void currentChanged();
        void updateLoop();
        void updateMidiStatus();
        void playFrom (double beat);

        void chooseFilesToImport();
        void chooseMidiExport();

        void pianoRollChanged() override;
        void arrangementChanged() override;

        JUCE_DECLARE_NON_COPYABLE (MainComponent)
    };
}
