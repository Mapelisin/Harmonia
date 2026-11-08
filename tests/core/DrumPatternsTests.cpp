#include <doctest.h>

#include "core/DrumPatterns.h"
#include "core/Drums.h"

using namespace harmonia::core;
namespace patterns = drum_patterns;

namespace
{
    constexpr int kick = 36, snare = 38, closedHat = 42, crash = 49, lowTom = 45, midTom = 47, highTom = 50;

    patterns::Settings plain (const juce::String& style, int bars = 1)
    {
        patterns::Settings s;

        for (size_t i = 0; i < patterns::getStyles().size(); ++i)
            if (patterns::getStyles()[i].name == style)
                s.style = (int) i;

        s.bars = bars;
        s.fill = patterns::Fill::none;
        s.crash = false;
        return s;
    }

    std::vector<double> startsOf (const std::vector<Note>& notes, int pitch)
    {
        std::vector<double> starts;

        for (const auto& n : notes)
            if (n.pitch == pitch)
                starts.push_back (n.startBeat);

        return starts;
    }

    const Note* find (const std::vector<Note>& notes, int pitch, double start)
    {
        for (const auto& n : notes)
            if (n.pitch == pitch && std::abs (n.startBeat - start) < 1.0e-9)
                return &n;

        return nullptr;
    }
}

TEST_SUITE ("core::drum_patterns")
{
    TEST_CASE ("a bar of rock: kick, snare on two and four, eighth-note hats accented on the beat")
    {
        const auto notes = patterns::generate (plain ("Rock"));
        CHECK (notes.size() == 3 + 2 + 8);
        CHECK (startsOf (notes, kick) == std::vector<double> { 0.0, 2.0, 2.5 });
        CHECK (startsOf (notes, snare) == std::vector<double> { 1.0, 3.0 });
        CHECK (startsOf (notes, closedHat) == std::vector<double> { 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5 });

        // X is the accent, x softer, o softer still.
        REQUIRE (find (notes, closedHat, 0.0) != nullptr);
        CHECK (find (notes, closedHat, 0.0)->velocity == 110);
        CHECK (find (notes, closedHat, 0.5)->velocity == 88);
        CHECK (find (notes, kick, 2.5)->velocity == 88);
        CHECK (find (notes, kick, 0.0)->lengthBeats == doctest::Approx (0.125));

        // In time order.
        CHECK (std::is_sorted (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.startBeat < b.startBeat; }));

        auto soft = plain ("Funk");
        soft.velocity = 100;
        const auto funk = patterns::generate (soft);
        REQUIRE (find (funk, snare, 1.75) != nullptr);
        CHECK (find (funk, snare, 1.75)->velocity == 45);       // a ghost note
    }

    TEST_CASE ("bars repeat from where it starts; another meter cuts the bar short")
    {
        auto s = plain ("Rock", 4);
        s.startBeat = 8.0;
        CHECK (patterns::getLength (s) == doctest::Approx (16.0));

        const auto notes = patterns::generate (s);
        CHECK (notes.size() == 4 * 13);
        CHECK (notes.front().startBeat == doctest::Approx (8.0));
        CHECK (notes.back().startBeat < 24.0);
        CHECK (startsOf (notes, snare) == std::vector<double> { 9.0, 11.0, 13.0, 15.0, 17.0, 19.0, 21.0, 23.0 });

        // 3/4: three beats a bar, the pattern's fourth beat left out.
        auto waltz = plain ("Rock", 2);
        waltz.beatsPerBar = 3.0;
        CHECK (patterns::getLength (waltz) == doctest::Approx (6.0));
        CHECK (startsOf (patterns::generate (waltz), snare) == std::vector<double> { 1.0, 4.0 });
    }

    TEST_CASE ("fills at the end of every four bars or only the last; a crash where each four begin")
    {
        auto s = plain ("Rock", 8);
        s.fill = patterns::Fill::everyFourthBar;
        auto notes = patterns::generate (s);

        // Bars 4 and 8: the last two beats are snare and toms coming down; no hats there.
        for (double barStart : { 12.0, 28.0 })
        {
            CHECK (find (notes, highTom, barStart + 3.0) != nullptr);
            CHECK (find (notes, midTom, barStart + 3.5) != nullptr);
            CHECK (find (notes, lowTom, barStart + 3.75) != nullptr);
            CHECK (find (notes, snare, barStart + 2.0) != nullptr);
            CHECK (find (notes, closedHat, barStart + 2.5) == nullptr);
            CHECK (find (notes, closedHat, barStart + 1.5) != nullptr);     // the groove up to there
        }

        CHECK (startsOf (notes, highTom).size() == 2 * 2);

        s.fill = patterns::Fill::lastBar;
        CHECK (startsOf (patterns::generate (s), highTom) == std::vector<double> { 31.0, 31.25 });

        s.fill = patterns::Fill::none;
        CHECK (startsOf (patterns::generate (s), highTom).empty());

        // The crash on bars 1 and 5, instead of the hat.
        s.crash = true;
        notes = patterns::generate (s);
        CHECK (startsOf (notes, crash) == std::vector<double> { 0.0, 16.0 });
        CHECK (find (notes, closedHat, 0.0) == nullptr);
        CHECK (find (notes, closedHat, 4.0) != nullptr);
    }

    TEST_CASE ("swing pushes every second sixteenth late; rolls are two quick hits")
    {
        auto s = plain ("Funk");
        s.swing = 1.0;
        const auto funk = patterns::generate (s);
        CHECK (find (funk, closedHat, 0.0) != nullptr);
        CHECK (find (funk, closedHat, 0.5) != nullptr);                     // eighths stay
        CHECK (find (funk, closedHat, 0.25 + 1.0 / 12.0) != nullptr);       // a triplet's lateness
        CHECK (find (funk, closedHat, 0.25) == nullptr);

        s.swing = 0.5;
        CHECK (find (patterns::generate (s), closedHat, 0.25 + 1.0 / 24.0) != nullptr);

        // Trap's hats roll through the last beat in thirty-seconds.
        const auto trap = patterns::generate (plain ("Trap"));
        std::vector<double> lastBeat;

        for (auto start : startsOf (trap, closedHat))
            if (start >= 3.0)
                lastBeat.push_back (start);

        CHECK (lastBeat == std::vector<double> { 3.0, 3.125, 3.25, 3.375, 3.5, 3.625, 3.75, 3.875 });
    }

    TEST_CASE ("every style is a playable bar of drums")
    {
        const auto& styles = patterns::getStyles();
        CHECK (styles.size() >= 10);

        for (size_t i = 0; i < styles.size(); ++i)
        {
            CAPTURE (styles[i].name);

            for (const auto& line : styles[i].lines)
            {
                CHECK (line.steps.length() == 16);
                CHECK (line.steps.containsOnly ("Xxor."));
                CHECK (drums::getName (line.pitch).isNotEmpty());
            }

            patterns::Settings s;
            s.style = (int) i;
            s.bars = 4;
            const auto notes = patterns::generate (s);
            CHECK (notes.size() > 4 * 8);
            CHECK (! startsOf (notes, kick).empty());

            // Never two hits of the same drum at once.
            for (size_t a = 1; a < notes.size(); ++a)
                for (size_t b = 0; b < a; ++b)
                    CHECK_FALSE ((notes[a].pitch == notes[b].pitch && std::abs (notes[a].startBeat - notes[b].startBeat) < 1.0e-9));

            for (const auto& n : notes)
            {
                CHECK (n.startBeat >= 0.0);
                CHECK (n.startBeat < 16.0);
                CHECK (n.velocity >= 1);
                CHECK (n.velocity <= 127);
            }
        }

        for (const auto& line : patterns::getFill())
            CHECK (line.steps.length() == 8);
    }
}
