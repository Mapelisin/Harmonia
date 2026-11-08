#pragma once

#include "core/Note.h"

/** Drum grooves written as notes (the piano roll's Drum pattern tool), on the keys of the General
    MIDI drum map (kick 36, snare 38, closed hat 42...) that most drum plugins and kits follow.
    Times are in beats; a beat is four steps (sixteenth notes), a bar of 4/4 sixteen. */
namespace harmonia::core::drum_patterns
{
    struct Line
    {
        int pitch = 36;
        juce::String steps;     // a step each: X accent, x hit, o soft (a ghost note), r two quick hits, . nothing
    };

    struct Style
    {
        juce::String name;      // "Rock", "House (four on the floor)"...
        std::vector<Line> lines;    // one bar of 4/4: 16 steps each
    };

    const std::vector<Style>& getStyles();

    /** The last two beats of a bar played as a fill: snare and toms coming down. */
    const std::vector<Line>& getFill();

    enum class Fill
    {
        none,
        everyFourthBar,     // the last bar of every four, and the last bar
        lastBar
    };

    inline constexpr int stepsPerBeat = 4;

    struct Settings
    {
        int style = 0;              // index into getStyles()
        int bars = 4;
        double beatsPerBar = 4.0;   // another meter cuts the 4/4 pattern short (3/4) or repeats its start (5/4)
        Fill fill = Fill::everyFourthBar;
        bool crash = true;          // a crash cymbal on the first beat of every four bars (in place of the hat)
        double swing = 0.0;         // 0 to 1: how late every second sixteenth comes (1: as late as in triplets)
        int velocity = 110;         // of the accents; hits are softer, ghost notes softer still
        double startBeat = 0.0;
    };

    /** The groove as notes, from `startBeat`. */
    std::vector<Note> generate (const Settings&);

    /** The length the groove takes: its bars. */
    double getLength (const Settings&);
}
