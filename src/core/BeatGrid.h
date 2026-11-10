#pragma once

#include "core/BeatFinder.h"

/** The grid of sixteenths the hits of a sound sit on: what the two ways of finding a beat share
    (one tempo for the whole sound: BeatFinder.cpp; a tempo followed along it: BeatFollower.cpp).
    Nothing else needs it. */
namespace harmonia::core::beat_finder::grid
{
    // Hits nearer than this to the grid are on it.
    inline constexpr double onGridSeconds = 0.015;

    // A beat has to explain this much of the hits (by their strength), and at least so many of them.
    inline constexpr double leastCertainty = 0.6;
    inline constexpr int fewestHits = 4;

    /** Sixteenths: start + n * step. */
    struct Grid
    {
        double start = 0.0, step = 0.0;
    };

    /** How far `value` is from the nearest whole multiple of `step` (signed). */
    double offGrid (double value, double step);

    /** The grid of sixteenths nearest `beatPeriod / 4` that the hits sit on best: looked for
        `spread` either way of it (0.025: two and a half percent), in steps of `increment`. */
    Grid bestGrid (const std::vector<Hit>& hits, double beatPeriod, double spread = 0.025, double increment = 0.0002);

    /** The straight line through the hits that are on the grid (their place in the grid against
        their time): the grid they really keep. Returns how much of the hits' strength is on it. */
    double refine (const std::vector<Hit>& hits, Grid& grid, double& offGridSeconds);
}
