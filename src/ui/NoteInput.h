#pragma once

#include "audio/NotePreview.h"
#include "audio/PlaybackClock.h"
#include "core/NoteRecorder.h"
#include "ui/ComputerKeyboard.h"
#include "ui/pianoroll/PianoRollModel.h"

namespace harmonia::ui
{
    /** Live notes: from the computer keyboard and from MIDI keyboards, heard on the edited
        track's instrument and, while recording, written into the edited clip.

        Recording follows FL Studio: Record arms it, and whenever the song plays while armed the
        played notes go into the clip. The clip loops, so every pass adds to what is there
        (overdub). A whole take, from Play to Stop, is one undo step. Notes are placed where the
        player heard the song when they pressed the key (see audio::PlaybackClock), then
        quantized if asked. Keys being held are shown in the piano roll before they are
        released and become notes. */
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

        /** A message from a MIDI keyboard (notes and the sustain pedal are used). */
        void handleMidiMessage (const juce::MidiMessage&, double wallMs);

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

    private:
        pianoroll::PianoRollModel& model;
        audio::NotePreview& preview;
        audio::SongClock& clock;

        ComputerKeyboard computerKeyboard;
        core::NoteRecorder recorder;
        std::optional<te::Edit::UndoTransactionInhibitor> keepOneUndoStep;
        core::note_ops::BeatRange takeLoop;
        double lastHeardBeat = 0.0;

        bool armed = false;
        bool sustainDown = false;
        core::InputQuantize quantize = core::InputQuantize::off;
        std::array<int, 128> held {};       // how many sources hold each pitch (for the keys on screen)

        void startTake();
        std::optional<double> heardBeat (double wallMs);
        void commit (const std::vector<core::Note>&);
        void commit (const std::optional<core::Note>&);
        void showHeld();
        void stateChanged();
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (NoteInput)
    };
}
