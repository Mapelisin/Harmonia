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
