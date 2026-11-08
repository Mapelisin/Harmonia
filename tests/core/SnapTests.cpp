#include <doctest.h>

#include "core/Snap.h"

using namespace harmonia::core;

TEST_SUITE ("core::snap")
{
    TEST_CASE ("grid sizes match the musical note values")
    {
        snap::Context context;
        context.beatsPerBar = 4.0;
        context.lineBeats = 0.5;

        CHECK (snap::getGridBeats (snap::Mode::none, context) == 0.0);
        CHECK (snap::getGridBeats (snap::Mode::line, context) == doctest::Approx (0.5));
        CHECK (snap::getGridBeats (snap::Mode::bar, context) == doctest::Approx (4.0));
        CHECK (snap::getGridBeats (snap::Mode::beat, context) == doctest::Approx (1.0));
        CHECK (snap::getGridBeats (snap::Mode::quarterBeat, context) == doctest::Approx (0.25));   // 1/16
        CHECK (snap::getGridBeats (snap::Mode::thirdBeat, context) == doctest::Approx (1.0 / 3.0)); // 1/8 triplet
        CHECK (snap::getGridBeats (snap::Mode::sixthStep, context) == doctest::Approx (1.0 / 24.0));

        for (auto mode : snap::getAllModes())
            CHECK (snap::getName (mode).isNotEmpty());
    }

    TEST_CASE ("the line grid follows the zoom")
    {
        // The finest line at least 10 pixels apart.
        CHECK (snap::getLineBeats (200.0, 4.0) == doctest::Approx (1.0 / 16.0));
        CHECK (snap::getLineBeats (60.0, 4.0) == doctest::Approx (0.25));
        CHECK (snap::getLineBeats (12.0, 4.0) == doctest::Approx (1.0));
        CHECK (snap::getLineBeats (2.0, 4.0) == doctest::Approx (8.0));     // whole bars, doubled until wide enough
    }

    TEST_CASE ("nearest, floor and delta snapping")
    {
        snap::Context context;

        CHECK (snap::snapNearest (1.13, snap::Mode::quarterBeat, context) == doctest::Approx (1.25));
        CHECK (snap::snapNearest (1.12, snap::Mode::quarterBeat, context) == doctest::Approx (1.0));
        CHECK (snap::snapFloor (1.99, snap::Mode::beat, context) == doctest::Approx (1.0));
        CHECK (snap::snapFloor (2.0, snap::Mode::beat, context) == doctest::Approx (2.0));
        CHECK (snap::snapNearest (1.13, snap::Mode::bar, context) == doctest::Approx (0.0));

        // Cell always goes to the start of the cell.
        context.lineBeats = 0.5;
        CHECK (snap::snapNearest (1.49, snap::Mode::cell, context) == doctest::Approx (1.0));

        // Moves are rounded to whole grid steps.
        CHECK (snap::snapDelta (0.7, snap::Mode::halfBeat, context) == doctest::Approx (0.5));
        CHECK (snap::snapDelta (-0.8, snap::Mode::halfBeat, context) == doctest::Approx (-1.0));
    }

    TEST_CASE ("without snap positions are still whole ticks")
    {
        snap::Context context;
        const auto snapped = snap::snapNearest (1.0 / 3.0 + 1.0e-7, snap::Mode::none, context);
        CHECK (snapped * snap::ticksPerBeat == doctest::Approx (std::round (snapped * snap::ticksPerBeat)));
        CHECK (snap::getStepBeats (snap::Mode::none, context) == doctest::Approx (0.25));
    }
}
