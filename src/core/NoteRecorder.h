#pragma once

#include "core/NoteOps.h"

namespace harmonia::core
{
    /** How recorded notes are put on the grid. */
    enum class InputQuantize
    {
        off,            // as played
        starts,         // starts snap to the grid, lengths stay as played
        startsAndEnds   // starts and ends snap to the grid
    };

    const std::vector<InputQuantize>& getAllInputQuantizeModes();
    juce::String getName (InputQuantize);

    /** Turns live key presses into notes. Pure logic: times are song beats *as the player heard
        them* (the caller compensates the audio latency), nothing here touches the clip.

        A take starts with start() and ends with stop(). While playback loops, a note still held
        when the loop wraps ends at the loop end. With the sustain pedal down, released keys keep
        sounding until the pedal comes up, so their notes end there. */
    class NoteRecorder
    {
    public:
        /** Starts a take. `loop` is the looped range in song beats (an empty range: no loop). */
        void start (note_ops::BeatRange loop);
        bool isActive() const noexcept                  { return active; }

        /** A key went down. Returns the previous note of the same key if it was still held. */
        std::optional<Note> noteOn (int pitch, int velocity, double beat);

        /** A key came up. Returns the finished note, unless the sustain pedal holds it. */
        std::optional<Note> noteOff (int pitch, double beat);

        /** Sustain pedal. Releasing it finishes the notes it was holding. */
        std::vector<Note> setSustain (bool down, double beat);

        /** Ends the take: finishes every note still sounding at `beat`. */
        std::vector<Note> stop (double beat);

        /** Notes still sounding, with their length up to `beat` (to show them while recording). */
        std::vector<Note> getSoundingNotes (double beat) const;

        /** Shortest recorded note, in beats. */
        static constexpr double minLengthBeats = 1.0 / 64.0;

    private:
        struct Sounding
        {
            int pitch = 0, velocity = 0;
            double startBeat = 0.0;
            bool released = false;      // key up, held by the pedal
        };

        bool active = false;
        bool sustain = false;
        note_ops::BeatRange loop;
        std::vector<Sounding> sounding;

        Note finish (const Sounding&, double beat) const;
        double getEnd (double startBeat, double beat) const;
    };

    /** Puts a recorded note on the grid. A start that snaps onto the loop end (played slightly
        early for the loop's first beat) moves to the loop start. */
    Note quantizeRecorded (const Note&, InputQuantize, double gridBeats, note_ops::BeatRange loop);
}
