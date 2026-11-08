#include <doctest.h>

#include "core/Progressions.h"

using namespace harmonia;
namespace progressions = core::progressions;
using core::Note;
using core::scales::Scale;
using core::scales::ScaleType;

namespace
{
    /** The pitches of each chord (notes starting together), in time order, low to high. */
    std::vector<std::vector<int>> chordsOf (const std::vector<Note>& notes)
    {
        std::map<double, std::vector<int>> byStart;

        for (const auto& n : notes)
            byStart[n.startBeat].push_back (n.pitch);

        std::vector<std::vector<int>> result;

        for (auto& [start, pitches] : byStart)
        {
            std::sort (pitches.begin(), pitches.end());
            result.push_back (pitches);
        }

        return result;
    }

    std::vector<Note> notesOf (std::initializer_list<std::pair<int, double>> pitchesAndLengths)
    {
        std::vector<Note> notes;
        double at = 0.0;

        for (const auto& [pitch, length] : pitchesAndLengths)
        {
            Note n;
            n.pitch = pitch;
            n.startBeat = at;
            n.lengthBeats = length;
            notes.push_back (n);
            at += length;
        }

        return notes;
    }
}

TEST_SUITE ("core::progressions")
{
    TEST_CASE ("I - V - vi - IV in C: one bar each, the voices moving as little as they can")
    {
        progressions::Settings s;
        const auto notes = progressions::generate (s);

        REQUIRE (notes.size() == 12);
        CHECK (chordsOf (notes) == std::vector<std::vector<int>> { { 60, 64, 67 }, { 59, 62, 67 }, { 60, 64, 69 }, { 60, 65, 69 } });

        for (const auto& n : notes)
        {
            CHECK (n.lengthBeats == doctest::Approx (4.0));
            CHECK (n.velocity == 100);
        }

        CHECK (progressions::getLength (s) == doctest::Approx (16.0));

        s.smooth = false;
        CHECK (chordsOf (progressions::generate (s)) == std::vector<std::vector<int>> { { 60, 64, 67 }, { 67, 71, 74 }, { 69, 72, 76 }, { 65, 69, 72 } });
    }

    TEST_CASE ("sevenths, a minor key, the octave and where it starts")
    {
        progressions::Settings s;
        s.sevenths = true;
        s.smooth = false;
        CHECK (chordsOf (progressions::generate (s)).front() == std::vector<int> { 60, 64, 67, 71 });     // Cmaj7

        // i - VI - III - VII in A minor: the tonic chord sits around C4, starting on A3.
        s.sevenths = false;
        s.key = { 9, ScaleType::minor };
        s.progression = 6;
        s.startBeat = 8.0;
        const auto notes = progressions::generate (s);
        CHECK (chordsOf (notes) == std::vector<std::vector<int>> { { 57, 60, 64 }, { 65, 69, 72 }, { 60, 64, 67 }, { 67, 71, 74 } });
        CHECK (notes.front().startBeat == doctest::Approx (8.0));

        s.octave = 3;
        CHECK (chordsOf (progressions::generate (s)).front() == std::vector<int> { 45, 48, 52 });

        // A chromatic "key" counts as major.
        s.key = {};
        s.progression = 0;
        s.octave = 4;
        CHECK (chordsOf (progressions::generate (s)).front() == std::vector<int> { 60, 64, 67 });
    }

    TEST_CASE ("rhythms, the bass and repeats")
    {
        progressions::Settings s;
        s.rhythm = progressions::Rhythm::quarters;
        auto notes = progressions::generate (s);
        CHECK (notes.size() == 48);
        CHECK (chordsOf (notes).size() == 16);
        CHECK (notes.front().lengthBeats == doctest::Approx (0.9));

        s.rhythm = progressions::Rhythm::eighths;
        CHECK (progressions::generate (s).size() == 96);

        s.rhythm = progressions::Rhythm::offbeats;
        notes = progressions::generate (s);
        CHECK (notes.size() == 48);
        CHECK (notes.front().startBeat == doctest::Approx (0.5));

        // Half a bar each: still in time, a chord on each beat.
        s.rhythm = progressions::Rhythm::quarters;
        s.chordBeats = 2.0;
        CHECK (progressions::generate (s).size() == 24);

        s.chordBeats = 4.0;
        s.rhythm = progressions::Rhythm::hold;
        s.bass = true;
        notes = progressions::generate (s);
        REQUIRE (notes.size() == 16);
        std::vector<int> bass;

        for (const auto& chord : chordsOf (notes))
            bass.push_back (chord.front());

        CHECK (bass == std::vector<int> { 48, 55, 57, 53 });      // C, G, A, F an octave below

        s.bass = false;
        s.repeats = 3;
        notes = progressions::generate (s);
        CHECK (notes.size() == 36);
        CHECK (notes.back().startBeat == doctest::Approx (44.0));
        CHECK (progressions::getLength (s) == doctest::Approx (48.0));
    }

    TEST_CASE ("every progression gives notes in every key")
    {
        progressions::Settings s;

        for (int p = 0; p < (int) progressions::getAll().size(); ++p)
        {
            for (int root = 0; root < 12; ++root)
            {
                for (auto type : { ScaleType::major, ScaleType::minor })
                {
                    s.progression = p;
                    s.key = { root, type };
                    s.sevenths = (root % 2) == 1;
                    const auto notes = progressions::generate (s);

                    CHECK (notes.size() == progressions::getAll()[(size_t) p].degrees.size() * (s.sevenths ? 4 : 3));

                    for (const auto& n : notes)
                    {
                        CHECK (s.key.contains (n.pitch));
                        CHECK (n.pitch >= 36);
                        CHECK (n.pitch <= 96);
                    }
                }
            }
        }
    }

    TEST_CASE ("the key is guessed from the notes")
    {
        // A C major scale with a C chord held.
        CHECK (core::scales::detect (notesOf ({ { 60, 2 }, { 62, 1 }, { 64, 1 }, { 65, 1 }, { 67, 1 }, { 69, 1 },
                                                { 71, 1 }, { 72, 2 }, { 64, 2 }, { 67, 2 } }))
               == Scale { 0, ScaleType::major });

        // An A minor tune with its G sharp.
        CHECK (core::scales::detect (notesOf ({ { 57, 2 }, { 60, 1 }, { 64, 1 }, { 69, 2 }, { 71, 1 }, { 68, 1 },
                                                { 69, 2 }, { 64, 1 }, { 62, 1 }, { 60, 1 } }))
               == Scale { 9, ScaleType::minor });

        // G major, with its F sharp.
        CHECK (core::scales::detect (notesOf ({ { 67, 2 }, { 71, 1 }, { 74, 1 }, { 66, 1 }, { 67, 2 }, { 62, 1 },
                                                { 64, 1 }, { 72, 1 } }))
               == Scale { 7, ScaleType::major });

        CHECK (core::scales::detect ({}).isChromatic());
    }
}
