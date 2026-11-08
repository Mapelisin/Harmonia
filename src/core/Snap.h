#pragma once

#include <juce_core/juce_core.h>

namespace harmonia::core::snap
{
    /*  Snapping positions to the grid (FL-style snap values).

        All times are in beats (quarter notes). Unsnapped positions are still rounded to whole
        ticks so that "no snap" never produces values like 0.30000000004. */

    constexpr int ticksPerBeat = 960;

    enum class Mode
    {
        none,
        line,           // the finest visible grid line (depends on zoom)
        cell,           // like line, but always the start of the cell the position is in
        bar,
        beat,           // 1/4 note
        halfBeat,       // 1/8
        thirdBeat,      // 1/8 triplet
        quarterBeat,    // 1/16 = one step
        sixthBeat,      // 1/16 triplet
        halfStep,       // 1/32
        thirdStep,      // 1/32 triplet
        quarterStep,    // 1/64
        sixthStep       // 1/64 triplet
    };

    /** All modes in menu order. */
    const std::vector<Mode>& getAllModes();

    juce::String getName (Mode);

    struct Context
    {
        double beatsPerBar = 4.0;
        double lineBeats = 0.25;     // spacing of the finest visible grid line
    };

    /** Grid size in beats; 0 for Mode::none. */
    double getGridBeats (Mode, const Context&);

    /** Finest grid line spacing (from 1/16 beat up to many bars) that is at least `minPixels` apart. */
    double getLineBeats (double pixelsPerBeat, double beatsPerBar, double minPixels = 10.0);

    double roundToTicks (double beats);

    /** Nearest grid line; for Mode::cell the start of the cell; for Mode::none the nearest tick. */
    double snapNearest (double beat, Mode, const Context&);

    /** Start of the grid cell that contains `beat` (where a click places a new note). */
    double snapFloor (double beat, Mode, const Context&);

    /** Rounds a move distance to whole grid steps, so moved notes keep their offset from the grid. */
    double snapDelta (double deltaBeats, Mode, const Context&);

    /** The grid size to use for lengths and keyboard nudges: the grid, or one step without snap. */
    double getStepBeats (Mode, const Context&);
}
