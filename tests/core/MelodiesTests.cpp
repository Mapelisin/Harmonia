#include <doctest.h>

#include "core/Melodies.h"

using namespace harmonia;
using namespace harmonia::core;
namespace melodies = core::melodies;

namespace
{
    Note makeNote (int pitch, double start, double length)
    {
        Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }

    /** C - Am - F - G, a bar each (4/4), twice: 8 bars of chords as a piano plays them. */
    std::vector<Note> pianoChords()
    {
        std::vector<Note> notes;
        const std::array<std::array<int, 3>, 4> chords { { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } } };

        for (int bar = 0; bar < 8; ++bar)
            for (auto pitch : chords[(size_t) (bar % 4)])
                notes.push_back (makeNote (pitch, bar * 4.0, 4.0));

        return notes;
    }

    std::vector<melodies::Chord> chordsOf (const std::vector<Note>& harmony)
    {
        return melodies::getChords (chords::findChords (harmony), harmony);
    }

    bool holds (const melodies::Chord& chord, int pitch)
    {
        return std::find (chord.pitchClasses.begin(), chord.pitchClasses.end(), pitch % 12) != chord.pitchClasses.end();
    }

    const melodies::Chord& chordAt (const std::vector<melodies::Chord>& chords, double beat)
    {
        for (const auto& c : chords)
            if (beat >= c.startBeat - 1.0e-9 && beat < c.endBeat - 1.0e-9)
                return c;

        return chords.back();
    }

    /** The note starts and lengths of a bar, from its start. */
    std::vector<std::pair<double, double>> rhythmOf (const std::vector<Note>& notes, double barStart, double beatsPerBar)
    {
        std::vector<std::pair<double, double>> rhythm;

        for (const auto& n : notes)
            if (n.startBeat >= barStart - 1.0e-9 && n.startBeat < barStart + beatsPerBar - 1.0e-9)
                rhythm.emplace_back (n.startBeat - barStart, n.lengthBeats);

        return rhythm;
    }
}

TEST_SUITE ("core::melodies")
{
    TEST_CASE ("the chords' notes: what sounds through at least half of each, and the root")
    {
        auto harmony = pianoChords();
        harmony.push_back (makeNote (74, 0.5, 0.25));       // a quick D over the C chord: not the chord's
        const auto chords = chordsOf (harmony);
        REQUIRE (chords.size() == 8);
        CHECK (chords[0] == melodies::Chord { 0.0, 4.0, 0, { 0, 4, 7 } });
        CHECK (chords[1] == melodies::Chord { 4.0, 8.0, 9, { 0, 4, 9 } });
        CHECK (chords[3].pitchClasses == std::vector<int> { 2, 7, 11 });

        // Not a chord: left out.
        CHECK (melodies::getChords ({ { 0.0, 4.0, "" } }, harmony).empty());
    }

    TEST_CASE ("the key: the song's, or the one the chords fit (the first chord's of two alike)")
    {
        const auto chords = chordsOf (pianoChords());
        CHECK (melodies::fitScale (chords, {}) == scales::Scale { 0, scales::ScaleType::major });

        const scales::Scale dorian { 2, scales::ScaleType::dorian };
        CHECK (melodies::fitScale (chords, dorian) == dorian);

        // Am - Dm - Em: the notes of C major too, but it starts on A minor.
        std::vector<Note> minor;

        for (auto [bar, pitches] : { std::pair { 0, std::array { 57, 60, 64 } }, std::pair { 1, std::array { 62, 65, 69 } },
                                     std::pair { 2, std::array { 64, 67, 71 } } })
            for (auto p : pitches)
                minor.push_back (makeNote (p, bar * 4.0, 4.0));

        CHECK (melodies::fitScale (chordsOf (minor), {}) == scales::Scale { 9, scales::ScaleType::minor });

        // E major in A minor (harmonic minor's dominant): still A minor.
        minor.push_back (makeNote (68, 12.0, 4.0));
        minor.push_back (makeNote (64, 12.0, 4.0));
        minor.push_back (makeNote (71, 12.0, 4.0));
        CHECK (melodies::fitScale (chordsOf (minor), {}) == scales::Scale { 9, scales::ScaleType::minor });
    }

    TEST_CASE ("a melody: one note at a time, in the key, chord tones on the beats, ending on the root")
    {
        const auto chords = chordsOf (pianoChords());
        const scales::Scale key { 0, scales::ScaleType::major };

        for (auto rhythm : melodies::getRhythms())
        {
            for (int variation = 1; variation <= 12; ++variation)
            {
                CAPTURE (melodies::getName (rhythm));
                CAPTURE (variation);
                melodies::Settings s;
                s.rhythm = rhythm;
                s.variation = variation;
                s.endBeat = 32.0;
                const auto notes = melodies::generate (chords, key, s);
                REQUIRE (notes.size() >= 8);

                for (size_t i = 0; i < notes.size(); ++i)
                {
                    const auto& n = notes[i];
                    CHECK (key.contains (n.pitch));
                    CHECK (n.pitch >= 67);
                    CHECK (n.pitch <= 84);
                    CHECK (n.startBeat >= 0.0);
                    CHECK (n.startBeat + n.lengthBeats <= 32.0 + 1.0e-9);
                    CHECK (n.velocity > 0);

                    if (i > 0)
                        CHECK (n.startBeat >= notes[i - 1].startBeat + notes[i - 1].lengthBeats - 1.0e-9);

                    if (std::abs (n.startBeat - std::round (n.startBeat)) < 1.0e-9)
                        CHECK (holds (chordAt (chords, n.startBeat), n.pitch));

                    // No wild leaps.
                    if (i > 0)
                        CHECK (std::abs (n.pitch - notes[i - 1].pitch) <= 12);
                }

                // Ends held on G's root, to the end.
                CHECK (notes.back().pitch % 12 == 7);
                CHECK (notes.back().startBeat + notes.back().lengthBeats == doctest::Approx (32.0));

                // Every bar starts with a note.
                for (int bar = 0; bar < 8; ++bar)
                    CHECK (! rhythmOf (notes, bar * 4.0, 4.0).empty());
            }
        }
    }

    TEST_CASE ("busier rhythms play more notes; the same variation is the same melody, another is another")
    {
        const auto chords = chordsOf (pianoChords());
        const scales::Scale key { 0, scales::ScaleType::major };
        std::map<melodies::Rhythm, size_t> counts;

        for (auto rhythm : melodies::getRhythms())
        {
            for (int variation = 1; variation <= 10; ++variation)
            {
                melodies::Settings s;
                s.rhythm = rhythm;
                s.variation = variation;
                s.endBeat = 32.0;
                counts[rhythm] += melodies::generate (chords, key, s).size();
            }
        }

        CHECK (counts[melodies::Rhythm::calm] < counts[melodies::Rhythm::medium]);
        CHECK (counts[melodies::Rhythm::medium] < counts[melodies::Rhythm::busy]);

        melodies::Settings s;
        s.endBeat = 32.0;
        const auto first = melodies::generate (chords, key, s);
        CHECK (melodies::generate (chords, key, s) == first);
        s.variation = 2;
        CHECK (melodies::generate (chords, key, s) != first);

        // Higher octave: an octave higher range.
        s.octave = 6;

        for (const auto& n : melodies::generate (chords, key, s))
            CHECK (n.pitch >= 79);
    }

    TEST_CASE ("phrases answer themselves: bars 3, 5 and 7 have the first bar's rhythm")
    {
        const auto chords = chordsOf (pianoChords());
        const scales::Scale key { 0, scales::ScaleType::major };

        for (int variation = 1; variation <= 8; ++variation)
        {
            CAPTURE (variation);
            melodies::Settings s;
            s.variation = variation;
            s.endBeat = 32.0;
            const auto notes = melodies::generate (chords, key, s);
            const auto first = rhythmOf (notes, 0.0, 4.0);

            for (auto bar : { 2, 4, 6 })
            {
                const auto again = rhythmOf (notes, bar * 4.0, 4.0);
                REQUIRE (again.size() == first.size());

                for (size_t i = 0; i < first.size(); ++i)
                {
                    CHECK (again[i].first == doctest::Approx (first[i].first));
                    CHECK (again[i].second == doctest::Approx (first[i].second));
                }
            }

            // Free: not bound to it (some variation differs).
            s.answer = false;
            const auto free = melodies::generate (chords, key, s);
            CHECK (free.size() > 0);
        }
    }

    TEST_CASE ("3/4, a short last bar, no chords at all")
    {
        const auto chords = chordsOf (pianoChords());
        const scales::Scale key { 0, scales::ScaleType::major };

        melodies::Settings s;
        s.beatsPerBar = 3.0;
        s.startBeat = 2.0;
        s.endBeat = 12.5;
        const auto notes = melodies::generate (chords, key, s);
        REQUIRE (! notes.empty());
        CHECK (notes.front().startBeat == doctest::Approx (2.0));
        CHECK (notes.back().startBeat + notes.back().lengthBeats == doctest::Approx (12.5));

        for (const auto& n : notes)
            CHECK (n.startBeat < 12.5);

        // Without chords: the key's own chord.
        const auto alone = melodies::generate ({}, key, s);
        REQUIRE (! alone.empty());
        CHECK (alone.back().pitch % 12 == 0);

        // Nothing to write.
        s.endBeat = s.startBeat;
        CHECK (melodies::generate (chords, key, s).empty());
    }
}
