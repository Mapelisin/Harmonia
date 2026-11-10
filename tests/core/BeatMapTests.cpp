#include <doctest.h>

#include "core/BeatMap.h"

using harmonia::core::BeatMap;

TEST_SUITE ("core: where the beats of a sound are")
{
    TEST_CASE ("a steady sound: so many beats a minute from the start of its file")
    {
        const BeatMap map (90.0);
        CHECK (map.isSteady());
        CHECK (map.toBeats (2.0) == doctest::Approx (3.0));
        CHECK (map.toSeconds (3.0) == doctest::Approx (2.0));
        CHECK (map.toBeats (-1.0) == doctest::Approx (-1.5));           // before the file, on the same line
        CHECK (map.toString() == "0.000000 90.000000");
    }

    TEST_CASE ("a sound that speeds up: each of its beats is a beat further, wherever it is in the file")
    {
        // Beats getting shorter: 0.5 s, 0.4 s, 0.3 s, the first a quarter of a second in.
        const std::vector<double> beats { 0.25, 0.75, 1.15, 1.45 };
        const auto map = BeatMap::fromBeats (beats);
        REQUIRE (map.has_value());
        CHECK_FALSE (map->isSteady());

        // The first beat is half a beat in, at the tempo the sound starts with.
        for (size_t i = 0; i < beats.size(); ++i)
        {
            CAPTURE (i);
            CHECK (map->toBeats (beats[i]) == doctest::Approx (0.5 + (double) i));
            CHECK (map->toSeconds (0.5 + (double) i) == doctest::Approx (beats[i]));
        }

        // Between beats, evenly; before the first and after the last, as the nearest beats go.
        CHECK (map->toBeats (0.95) == doctest::Approx (2.0));
        CHECK (map->toBeats (0.0) == doctest::Approx (0.0));
        CHECK (map->toSeconds (4.5) == doctest::Approx (1.75));

        // Kept in a clip as text and read back the same.
        const auto read = BeatMap::fromString (map->toString());
        REQUIRE (read.has_value());
        CHECK (read->getChanges().size() == 3);
        CHECK (read->toSeconds (2.5) == doctest::Approx (1.15).epsilon (1.0e-6));
        CHECK (read->toString() == map->toString());
    }

    TEST_CASE ("beats that keep one tempo make one tempo; what isn't a row of beats makes nothing")
    {
        const auto even = BeatMap::fromBeats ({ 0.1, 0.6, 1.1, 1.6, 2.1 });
        REQUIRE (even.has_value());
        CHECK (even->isSteady());
        CHECK (even->toBeats (0.6) == doctest::Approx (1.2));

        CHECK_FALSE (BeatMap::fromBeats ({ 1.0 }).has_value());
        CHECK_FALSE (BeatMap::fromBeats ({ 1.0, 0.5 }).has_value());
        CHECK_FALSE (BeatMap::fromBeats ({ -0.5, 0.5 }).has_value());

        CHECK_FALSE (BeatMap::fromString ("").has_value());
        CHECK_FALSE (BeatMap::fromString ("0 120 4").has_value());
        CHECK_FALSE (BeatMap::fromString ("1 120").has_value());                // doesn't begin at the file's start
        CHECK_FALSE (BeatMap::fromString ("0 120 4 0").has_value());
        CHECK_FALSE (BeatMap::fromString ("0 120 4 110 4 100").has_value());    // not going up
    }

    TEST_CASE ("counted in twice the beats, the same moments are twice as many beats in")
    {
        const auto map = BeatMap::fromBeats ({ 0.25, 0.75, 1.15, 1.45 });
        REQUIRE (map.has_value());
        const auto twice = map->scaled (2.0);

        for (const auto seconds : { 0.0, 0.25, 0.9, 1.15, 1.45, 2.0 })
        {
            CAPTURE (seconds);
            CHECK (twice.toBeats (seconds) == doctest::Approx (2.0 * map->toBeats (seconds)));
        }

        CHECK (twice.toSeconds (5.0) == doctest::Approx (1.15));
    }

    TEST_CASE ("a beat put elsewhere by hand: its neighbours and all beyond them stay, the tempo either side of it gives")
    {
        // 120 BPM: a beat every half second. The fifth beat's hit is 40 ms late.
        const BeatMap steady (120.0);
        const auto moved = steady.withBeatAt (5.0, 2.54);
        REQUIRE (moved.has_value());
        CHECK_FALSE (moved->isSteady());
        CHECK (moved->toSeconds (5.0) == doctest::Approx (2.54));

        for (const auto beat : { 0.0, 1.0, 3.5, 4.0, 6.0, 6.5, 20.0 })
        {
            CAPTURE (beat);
            CHECK (moved->toSeconds (beat) == doctest::Approx (steady.toSeconds (beat)));
        }

        // Evenly from the neighbours to it.
        CHECK (moved->toSeconds (4.5) == doctest::Approx (2.27));
        CHECK (moved->toSeconds (5.5) == doctest::Approx (2.77));
        CHECK (moved->getChanges().size() == 4);       // from the start, from 4, from 5, from 6

        // Not past a neighbour, nor nearer one than a quarter of the way it was.
        CHECK (steady.withBeatAt (5.0, 9.0)->toSeconds (5.0) == doctest::Approx (2.875));
        CHECK (steady.withBeatAt (5.0, 0.0)->toSeconds (5.0) == doctest::Approx (2.125));

        // The neighbours can be nearer (the lines of a finer grid) or further (bars).
        const auto fine = steady.withBeatAt (5.0, 2.52, 0.25);
        REQUIRE (fine.has_value());
        CHECK (fine->toSeconds (4.75) == doctest::Approx (2.375));
        CHECK (fine->toSeconds (5.0) == doctest::Approx (2.52));
        CHECK (fine->toSeconds (5.25) == doctest::Approx (2.625));

        // Within a beat of the file's start, the start is the neighbour; the start itself stays.
        const auto early = steady.withBeatAt (0.5, 0.3);
        REQUIRE (early.has_value());
        CHECK (early->toSeconds (0.0) == doctest::Approx (0.0));
        CHECK (early->toSeconds (0.5) == doctest::Approx (0.3));
        CHECK (early->toSeconds (1.5) == doctest::Approx (0.75));
        CHECK_FALSE (steady.withBeatAt (0.0, 0.1).has_value());

        // Moved again, it is where it is put last; the next beat moved, this one stays.
        const auto again = moved->withBeatAt (5.0, 2.48);
        REQUIRE (again.has_value());
        CHECK (again->toSeconds (5.0) == doctest::Approx (2.48));
        CHECK (again->toSeconds (4.0) == doctest::Approx (2.0));

        const auto next = moved->withBeatAt (6.0, 3.05);
        REQUIRE (next.has_value());
        CHECK (next->toSeconds (5.0) == doctest::Approx (2.54));
        CHECK (next->toSeconds (6.0) == doctest::Approx (3.05));
        CHECK (next->toSeconds (7.0) == doctest::Approx (3.5));

        // Let go, it is back between its neighbours: one tempo again. As text and back, the same.
        const auto back = moved->withoutChangeAt (5.0);
        REQUIRE (back.has_value());
        CHECK (back->isSteady());
        CHECK (back->toSeconds (5.0) == doctest::Approx (2.5));
        CHECK_FALSE (steady.withoutChangeAt (5.0).has_value());
        CHECK_FALSE (moved->withoutChangeAt (4.5).has_value());

        const auto read = BeatMap::fromString (moved->toString());
        REQUIRE (read.has_value());
        CHECK (read->toSeconds (5.0) == doctest::Approx (2.54).epsilon (1.0e-6));
        CHECK (read->toSeconds (9.0) == doctest::Approx (4.5).epsilon (1.0e-6));
    }

    TEST_CASE ("played backwards, the beats are as far from the end as they were from the start")
    {
        const auto map = BeatMap::fromBeats ({ 0.25, 0.75, 1.15, 1.45 });
        REQUIRE (map.has_value());
        const auto fileSeconds = 1.6;
        const auto allBeats = map->toBeats (fileSeconds);
        const auto back = map->reversed (fileSeconds);

        for (const auto seconds : { 0.0, 0.25, 0.75, 1.0, 1.15, 1.45, 1.6 })
        {
            CAPTURE (seconds);
            CHECK (back.toBeats (fileSeconds - seconds) == doctest::Approx (allBeats - map->toBeats (seconds)));
        }

        // A steady sound backwards is as steady.
        CHECK (BeatMap (100.0).reversed (3.0).isSteady());
        CHECK (BeatMap (100.0).reversed (3.0).toBeats (1.2) == doctest::Approx (2.0));
    }
}
