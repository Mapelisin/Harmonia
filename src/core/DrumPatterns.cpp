#include "core/DrumPatterns.h"

namespace harmonia::core::drum_patterns
{
    namespace
    {
        // General MIDI drum keys.
        constexpr int kick = 36, snare = 38, clap = 39, closedHat = 42, openHat = 46, crashCymbal = 49,
                      lowTom = 45, midTom = 47, highTom = 50;

        constexpr int patternSteps = 16;
        constexpr int fillSteps = 8;
        constexpr int phraseBars = 4;
        constexpr double stepBeats = 1.0 / stepsPerBeat;

        int velocityOf (juce::juce_wchar step, int accent)
        {
            switch (step)
            {
                case 'X':   return accent;
                case 'x':   return juce::roundToInt (accent * 0.8);
                case 'r':   return juce::roundToInt (accent * 0.7);
                case 'o':   return juce::roundToInt (accent * 0.45);
                default:    return 0;
            }
        }

        bool isFillBar (const Settings& s, int bar)
        {
            switch (s.fill)
            {
                case Fill::none:            return false;
                case Fill::lastBar:         return bar == s.bars - 1;
                case Fill::everyFourthBar:  return bar == s.bars - 1 || bar % phraseBars == phraseBars - 1;
            }

            return false;
        }
    }

    const std::vector<Style>& getStyles()
    {
        static const std::vector<Style> all {
            { "Rock", { { kick,      "X.......X.x....." },
                        { snare,     "....X.......X..." },
                        { closedHat, "X.x.x.x.X.x.x.x." } } },
            { "Pop", { { kick,      "X.....x.X......." },
                       { snare,     "....X.......X..." },
                       { clap,      "....x.......x..." },
                       { closedHat, "x.x.x.x.x.x.x.x." } } },
            { "House (four on the floor)", { { kick,      "X...X...X...X..." },
                                             { clap,      "....X.......X..." },
                                             { closedHat, "o...o...o...o..." },
                                             { openHat,   "..x...x...x...x." } } },
            { "Disco", { { kick,      "X...X...X...X..." },
                         { snare,     "....X.......X..." },
                         { closedHat, "xo.oxo.oxo.oxo.o" },
                         { openHat,   "..x...x...x...x." } } },
            { "Hip-hop (boom bap)", { { kick,      "X......X..X....." },
                                      { snare,     "....X.......X..." },
                                      { closedHat, "x.x.x.x.x.x.x.x." } } },
            { "Trap", { { kick,      "X......X.X......" },
                        { clap,      "........X......." },
                        { closedHat, "x.x.x.x.x.x.rrrr" } } },
            { "Reggaeton (dembow)", { { kick,      "X...X...X...X..." },
                                      { snare,     "...X..X....X..X." },
                                      { closedHat, "x.x.x.x.x.x.x.x." } } },
            { "Funk", { { kick,      "X.....x...X..x.." },
                        { snare,     "....X..o.o..X..o" },
                        { closedHat, "XoxoXoxoXoxoXoxo" } } },
            { "Drum and bass", { { kick,      "X.........X....." },
                                 { snare,     "....X.......X..." },
                                 { closedHat, "x.x.x.x.x.x.x.x." } } },
            { "Half-time", { { kick,      "X.........x....." },
                             { snare,     "........X......." },
                             { closedHat, "x.x.x.x.x.x.x.x." } } },
        };

        return all;
    }

    const std::vector<Line>& getFill()
    {
        static const std::vector<Line> fill { { kick,    "X...X..." },
                                              { snare,   "Xoxx...." },
                                              { highTom, "....xx.." },
                                              { midTom,  "......x." },
                                              { lowTom,  ".......X" } };
        return fill;
    }

    double getLength (const Settings& s)
    {
        return std::max (1, s.bars) * s.beatsPerBar;
    }

    std::vector<Note> generate (const Settings& s)
    {
        const auto& styles = getStyles();
        const auto& style = styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, s.style)];
        const auto barSteps = std::max (1, juce::roundToInt (s.beatsPerBar * stepsPerBeat));
        const auto fillStart = std::max (0, barSteps - fillSteps);
        const auto accent = juce::jlimit (1, 127, s.velocity);
        const auto swingBeats = juce::jlimit (0.0, 1.0, s.swing) * stepBeats / 3.0;
        std::vector<Note> notes;

        const auto add = [&notes] (int pitch, double start, double length, int velocity)
        {
            Note n;
            n.pitch = pitch;
            n.startBeat = start;
            n.lengthBeats = length;
            n.velocity = juce::jlimit (1, 127, velocity);
            notes.push_back (n);
        };

        const auto play = [&] (const Line& line, int step, int index, double barStart)
        {
            const auto symbol = line.steps[index];
            const auto velocity = velocityOf (symbol, accent);

            if (velocity <= 0)
                return;

            // Every second sixteenth is pushed late by the swing.
            const auto start = barStart + step * stepBeats + (step % 2 == 1 ? swingBeats : 0.0);

            if (symbol == 'r')
            {
                add (line.pitch, start, stepBeats / 4.0, velocity);
                add (line.pitch, start + stepBeats / 2.0, stepBeats / 4.0, velocity);
            }
            else
            {
                add (line.pitch, start, stepBeats / 2.0, velocity);
            }
        };

        for (int bar = 0; bar < std::max (1, s.bars); ++bar)
        {
            const auto barStart = s.startBeat + bar * s.beatsPerBar;
            const auto fill = isFillBar (s, bar);
            const auto crash = s.crash && bar % phraseBars == 0;

            for (int step = 0; step < barSteps; ++step)
            {
                if (fill && step >= fillStart)
                {
                    for (const auto& line : getFill())
                        play (line, step, (step - fillStart) % line.steps.length(), barStart);

                    continue;
                }

                for (const auto& line : style.lines)
                {
                    // The crash takes the place of a hat.
                    if (crash && step == 0 && (line.pitch == closedHat || line.pitch == openHat))
                        continue;

                    play (line, step, step % patternSteps, barStart);
                }

                if (crash && step == 0)
                    add (crashCymbal, barStart, stepBeats * 2.0, accent);
            }
        }

        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
        {
            return std::tie (a.startBeat, a.pitch) < std::tie (b.startBeat, b.pitch);
        });

        return notes;
    }
}
