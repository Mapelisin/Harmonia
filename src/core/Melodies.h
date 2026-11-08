#pragma once

#include "core/Scales.h"

/** Melodies written as notes over the chords the song plays (the piano roll's Melody tool): chord
    tones on the beats, steps of the key between them, in four-bar phrases that rise and come back
    to rest, the third bar answering the first. A sketch to keep or change, not a finished tune.
    Times are in song beats. */
namespace harmonia::core::melodies
{
    /** A chord the melody goes over: when, its root and the pitch classes it holds (0 = C). */
    struct Chord
    {
        double startBeat = 0.0, endBeat = 0.0;
        int root = 0;
        std::vector<int> pitchClasses;      // sorted, the root among them

        bool operator== (const Chord&) const = default;
    };

    /** The chords named in `spans` with the notes they hold: the pitch classes `harmony` plays
        through at least half of each (so a quick melody note over a chord doesn't count), and
        always its root. Spans that aren't chords are left out. */
    std::vector<Chord> getChords (const std::vector<chords::Span>& spans, const std::vector<Note>& harmony);

    /** The key to write in: the song's scale if it has one, else the major or minor key holding
        most of the chords' notes (the first chord's key when several do). */
    scales::Scale fitScale (const std::vector<Chord>&, const scales::Scale& songScale);

    enum class Rhythm
    {
        calm,       // quarters and long notes
        medium,     // quarters and eighths
        busy        // eighths and sixteenths
    };

    const std::vector<Rhythm>& getRhythms();
    juce::String getName (Rhythm);

    struct Settings
    {
        Rhythm rhythm = Rhythm::medium;
        int octave = 5;             // around C of this octave: 5 is C5 (72)
        int variation = 1;          // another number, another melody (the same number, the same one)
        bool answer = true;         // the third bar of each phrase starts as the first did
        int velocity = 96;          // on the first beat of a bar; a little softer after it
        double beatsPerBar = 4.0;
        double startBeat = 0.0;     // where the bars start
        double endBeat = 16.0;      // where the melody stops
    };

    /** A melody from `startBeat` to `endBeat` over the chords, in the scale. One note at a time;
        a note starting on a beat is a tone of the chord there; the last one, held, its root. */
    std::vector<Note> generate (const std::vector<Chord>&, const scales::Scale&, const Settings&);
}
