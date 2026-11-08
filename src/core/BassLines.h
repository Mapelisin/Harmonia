#pragma once

#include "core/Scales.h"
#include "core/Tracktion.h"

/** Bass parts written as notes under the chords the song plays (the piano roll's Bass line
    tool): the chords come from the other tracks' notes, the bass follows their roots in one of a
    few common patterns. Times are in song beats. */
namespace harmonia::core::bass_lines
{
    /** The pitch class (0 = C ... 11 = B) of a chord symbol's root ("Am7" -> 9, "F#" -> 6,
        "C/E" -> 0), or with `bass` of its lowest note ("C/E" -> 4; without a slash, the root).
        -1 if it isn't a chord symbol. */
    int rootOf (const juce::String& chordName, bool bass = false);

    enum class Pattern
    {
        held,           // one note for the whole chord
        beats,          // the root on every beat
        eighths,        // on every half beat (driving rock, pop)
        rootFifth,      // root and fifth in turn on the beats (country, marches)
        octaves,        // root and the octave above in eighths (disco)
        walking         // through the chord on the beats, the last one leading into the next chord (jazz, blues)
    };

    const std::vector<Pattern>& getPatterns();
    juce::String getName (Pattern);

    struct Settings
    {
        Pattern pattern = Pattern::beats;
        int octave = 2;             // the roots sit in this octave: 2 is C2 (36) to B2 (47)
        bool slashBass = true;      // under "C/E" play E, as the chord asks; else its root
        double gate = 0.9;          // how much of each step a note lasts
        int velocity = 100;         // on the beats; the notes between them are a little softer
    };

    /** A bass part under the chords: each chord's notes from its start to its end. */
    std::vector<Note> generate (const std::vector<chords::Span>&, const Settings&);

    /** What the song's other tracks play from `startBeat` to `endBeat` (song beats), cut to that
        stretch: the harmony a bass follows. Not the notes of `except`, of drum tracks, of muted
        tracks or clips, nor muted notes. */
    std::vector<Note> getHarmony (te::Edit&, const te::Track* except, double startBeat, double endBeat);
}
