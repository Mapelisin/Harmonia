#pragma once

#include "audio/NotePreview.h"
#include "audio/PlaybackClock.h"
#include "core/NoteRecorder.h"
#include "core/TakeClips.h"
#include "ui/ComputerKeyboard.h"
#include "ui/pianoroll/PianoRollModel.h"

namespace harmonia::ui
{
    /** Live notes: from the computer keyboard and from MIDI keyboards, heard on the edited
        track's instrument and, while recording, written into the edited clip.

        Recording follows FL Studio: Record arms it, and whenever the song plays while armed the
        played notes go into the clip open in the piano roll. In clip mode playback loops the
        clip, so every pass adds to what is there (overdub). In song mode the song plays on past
        the clip: what is played there isn't lost, it goes into the clip the track has at that
        place or into a clip made for the take (core::TakeClips), and so does everything played
        while no clip is open at all. A whole take, from Play to Stop, is one undo step. Notes are
        placed where the player heard the song when they pressed the key (see
        audio::PlaybackClock), then quantized if asked. Keys being held are shown in the piano roll before they are
        released and become notes. The pitch-bend and mod wheels are recorded into the clip
        as they move (core::controllers); the sustain pedal makes the notes longer instead. */
    class NoteInput final : private juce::Timer
    {
    public:
        NoteInput (pianoroll::PianoRollModel&, audio::NotePreview&, audio::SongClock&);
        ~NoteInput() override;

        enum class Source
        {
            computerKeyboard,   // heard through NotePreview
            midiKeyboard        // the engine plays it on the track by itself (audio::MidiInputs)
        };

        static double now()     { return juce::Time::getMillisecondCounterHiRes(); }

        void noteOn (int pitch, int velocity, Source, double wallMs = now());
        void noteOff (int pitch, Source, double wallMs = now());
        void setSustain (bool down, double wallMs = now());

        /** A message from a MIDI keyboard (notes, the sustain pedal and the wheels are used). */
        void handleMidiMessage (const juce::MidiMessage&, double wallMs);

        /** The track that is played (the current one): what is played while no clip is open in
            the piano roll is recorded on it. With a clip open, that clip's track is the one. */
        void setTrack (te::AudioTrack*);

        void setRecordArmed (bool);
        bool isRecordArmed() const noexcept                 { return armed; }
        bool isRecording() const noexcept                   { return recorder.isActive(); }

        void setQuantize (core::InputQuantize);
        core::InputQuantize getQuantize() const noexcept    { return quantize; }

        ComputerKeyboard& getComputerKeyboard() noexcept    { return computerKeyboard; }

        /** Starts a take when playback starts while armed, finishes it when playback stops or
            recording is disarmed, and refreshes the held notes on screen. Runs on a timer;
            public so tests can step it. */
        void update();

        /** Finishes the current take now (e.g. before the edited clip changes). */
        void finishTake();

        /** Armed, recording or quantize changed (for buttons). */
        std::function<void()> onStateChanged;

        /** A take has ended that made clips of its own, for what was played where the track had
            none: those clips, in the order they were made. */
        std::function<void (const juce::Array<te::MidiClip*>&)> onClipsMade;

    private:
        pianoroll::PianoRollModel& model;
        audio::NotePreview& preview;
        audio::SongClock& clock;

        ComputerKeyboard computerKeyboard;
        core::NoteRecorder recorder;
        std::optional<te::Edit::UndoTransactionInhibitor> keepOneUndoStep;
        core::note_ops::BeatRange takeLoop, takeClipRange, takeWholeClip;   // the clip's loop, the whole clip
        juce::ReferenceCountedObjectPtr<te::MidiClip> takeClip;
        juce::ReferenceCountedObjectPtr<te::AudioTrack> track;
        std::unique_ptr<core::TakeClips> elsewhere;     // what is played outside the open clip goes there
        double lastHeardBeat = 0.0;

        bool armed = false;
        bool sustainDown = false;
        core::InputQuantize quantize = core::InputQuantize::off;
        std::array<int, 128> held {};       // how many sources hold each pitch (for the keys on screen)

        void startTake();
        te::AudioTrack* getRecordedTrack() const;
        std::optional<double> heardBeat (double wallMs);
        void commit (const std::vector<core::Note>&);
        void commit (const std::optional<core::Note>&);
        std::vector<core::Note> keepInsideClip (std::vector<core::Note>) const;
        std::optional<double> placeInsideClip (double beat) const;
        void recordController (core::controllers::Controller, float value, double wallMs);
        void showHeld();
        void stateChanged();
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (NoteInput)
    };
}
