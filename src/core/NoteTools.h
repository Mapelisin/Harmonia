#pragma once

#include "core/NoteOps.h"

namespace harmonia::core
{
    /** The piano roll's tools with settings (they open a window and change the notes live as the
        knobs move). Pure functions: notes in, notes out; times in beats. */
    namespace note_tools
    {
        /** What a tool did: the given notes' new values (same order), which of them to remove
            and the notes to add. */
        struct Result
        {
            std::vector<Note> notes;
            std::vector<size_t> removed;
            std::vector<Note> added;
        };

        /** Notes starting at the same moment: a chord (or a single note). Indices into the given
            notes, chords in time order, each one from its lowest note up. */
        std::vector<std::vector<size_t>> findChords (const std::vector<Note>&);

        //==============================================================================
        enum class StrumDirection
        {
            up,             // from the lowest note, like a down-stroke on a guitar
            down,
            alternate       // up, down, up... chord by chord
        };

        struct StrumSettings
        {
            double timeBeats = 0.125;       // how late the last note of a chord comes
            double tension = 0.0;           // -1..1: the notes come faster at the start (> 0) or at the end (< 0)
            StrumDirection direction = StrumDirection::up;
            double velocity = 0.0;          // -1..1: the last note's velocity this much lower or higher
            bool keepEnds = true;           // delayed notes still end where they did
        };

        /** Strum: spreads the notes of each chord in time, like a guitar. */
        std::vector<Note> strum (std::vector<Note>, const StrumSettings&);

        //==============================================================================
        struct HumanizeSettings
        {
            double timingBeats = 0.0;       // starts move up to this much either way
            double velocity = 0.0;          // velocities change up to this much either way (MIDI units)
            double length = 0.0;            // 0..1: lengths change up to this share either way
            int seed = 1;                   // the same seed gives the same result
        };

        /** Humanize: small random changes, so a part sounds played rather than programmed.
            Starts never go below `minStart`. */
        std::vector<Note> humanize (std::vector<Note>, const HumanizeSettings&, double minStart = 0.0);

        //==============================================================================
        struct ScaleLevelsSettings
        {
            NoteProperty property = NoteProperty::velocity;
            double multiply = 1.0;          // 0..2: spread of the values (around the middle for pan and fine pitch)
            double offset = 0.0;            // -1..1 of the full range: all values up or down
        };

        /** Scale levels: makes a property (velocity...) of the notes louder, softer, more or less even. */
        std::vector<Note> scaleLevels (std::vector<Note>, const ScaleLevelsSettings&);

        //==============================================================================
        enum class ArpPattern
        {
            up,
            down,
            upDown,         // up and back, the top and bottom notes played once
            downUp
        };

        struct ArpSettings
        {
            ArpPattern pattern = ArpPattern::up;
            double stepBeats = 0.25;        // one arpeggio note every step
            double gate = 0.9;              // 0.05..1: note length as a share of the step
            int octaves = 1;                // 1..4: the chord repeated this many octaves up
        };

        /** Arpeggiate: every chord (two or more notes starting together) is replaced by its notes
            one after another, for as long as its longest note. Each arpeggio note has the values
            (velocity...) of the chord note it comes from. Single notes stay as they are. */
        Result arpeggiate (const std::vector<Note>&, const ArpSettings&);
    }
}
