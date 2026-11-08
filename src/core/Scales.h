#pragma once

#include "core/Tracktion.h"

namespace harmonia::core
{
    /** Musical scales (the song's key) and chords, for highlighting rows, snapping notes to
        the scale and stamping chords in the piano roll. Pitches are MIDI note numbers. */
    namespace scales
    {
        enum class ScaleType
        {
            chromatic,          // no scale: every note fits
            major,
            minor,              // natural minor
            harmonicMinor,
            melodicMinor,
            dorian,
            phrygian,
            lydian,
            mixolydian,
            majorPentatonic,
            minorPentatonic,
            blues
        };

        const std::vector<ScaleType>& getAllTypes();
        juce::String getName (ScaleType);

        /** Semitones above the root, from 0. */
        const std::vector<int>& getIntervals (ScaleType);

        /** "C", "C#"... for 0..11. */
        juce::String getRootName (int root);

        struct Scale
        {
            int root = 0;               // 0 = C ... 11 = B
            ScaleType type = ScaleType::chromatic;

            bool isChromatic() const noexcept       { return type == ScaleType::chromatic; }

            bool contains (int pitch) const;
            bool isRoot (int pitch) const;

            /** The nearest pitch in the scale (the lower one when two are as near), within 0..127. */
            int snap (int pitch) const;

            /** The pitch `steps` scale degrees above (or below) `pitch`, which is snapped first;
                within 0..127 (it stops at the ends). */
            int step (int pitch, int steps) const;

            /** How many scale degrees from `from` up to `to` (negative going down), both snapped first. */
            int countSteps (int from, int to) const;

            /** "C major", "A minor", or "No scale". */
            juce::String getName() const;

            bool operator== (const Scale&) const = default;
        };

        /** The song's scale, kept in the edit's state (saved with the project). */
        Scale read (const te::Edit&);

        /** Sets the song's scale, recording undo actions in `um` (also what marks the project
            as changed). */
        void write (te::Edit&, const Scale&, juce::UndoManager* um);
    }

    namespace chords
    {
        enum class ChordType
        {
            major,
            minor,
            diminished,
            augmented,
            sus2,
            sus4,
            major7,
            minor7,
            dominant7,
            add9,
            power,
            scaleTriad,     // every other note of the scale from the root: the chord the scale gives there
            scaleSeventh    // the same with four notes
        };

        const std::vector<ChordType>& getAllTypes();
        juce::String getName (ChordType);

        /** The chord's pitches from `root` up (notes above 127 left out). Scale chords take the
            root to the scale first; with no scale they are major and dominant seventh. */
        std::vector<int> build (int root, ChordType, const scales::Scale&);

        /** The chord these pitches make, as a chord symbol: "C", "Am7", "Gsus4", "C/E" (with
            another note in the bass)... Octaves and repeated notes don't matter. Empty for a
            single note or a mix that isn't a common chord. */
        juce::String nameOf (const std::vector<int>& pitches);
    }
}
