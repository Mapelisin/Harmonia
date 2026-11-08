#pragma once

#include "core/Note.h"
#include "core/Scales.h"

namespace harmonia::core
{
    /** Chord progressions written as notes (the piano roll's Chord progression tool), and
        guessing a part's key from its notes. Times are in beats. */
    namespace progressions
    {
        struct Progression
        {
            juce::String name;              // "I - V - vi - IV  (pop)"
            std::vector<int> degrees;       // scale degrees of the chords' roots, 0 = the tonic
        };

        const std::vector<Progression>& getAll();

        enum class Rhythm
        {
            hold,           // one long chord
            quarters,       // a chord on every beat
            eighths,        // on every half beat
            offbeats        // on the "and" of every beat (house stabs)
        };

        const std::vector<Rhythm>& getRhythms();
        juce::String getName (Rhythm);

        struct Settings
        {
            scales::Scale key { 0, scales::ScaleType::major };
            int progression = 0;            // index into getAll()
            bool sevenths = false;          // four-note chords
            double chordBeats = 4.0;        // how long each chord lasts
            Rhythm rhythm = Rhythm::hold;
            int octave = 4;                 // the tonic chord sits around C of it (C4 = 60; A minor starts on A3)
            bool smooth = true;             // inversions that move each voice as little as possible
            bool bass = false;              // the root an octave below the chord, held
            int repeats = 1;
            int velocity = 100;
            double startBeat = 0.0;
        };

        /** The progression as notes, from `startBeat`. A chromatic key counts as major. */
        std::vector<Note> generate (const Settings&);

        /** The total length the progression takes. */
        double getLength (const Settings&);
    }

    namespace scales
    {
        /** The major or minor key these notes most likely are in (Krumhansl-Schmuckler: how
            long each of the 12 notes sounds, compared with how much each key uses them); a
            chromatic Scale if there are no notes. */
        Scale detect (const std::vector<Note>&);
    }
}
