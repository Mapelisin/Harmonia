#include "core/NoteTools.h"
#include "core/Snap.h"

#include <numeric>

namespace harmonia::core::note_tools
{
    namespace
    {
        // Notes are stored in whole ticks: half a tick tells "the same time" apart.
        constexpr double sameTime = 0.5 / snap::ticksPerBeat;
        constexpr double oneTick = 1.0 / snap::ticksPerBeat;

        /** 0..1 -> 0..1, bent by `tension` (-1..1): > 0 starts slowly, < 0 starts quickly. */
        double bend (double x, double tension)
        {
            tension = juce::jlimit (-1.0, 1.0, tension);

            if (tension >= 0.0)
                return std::pow (x, 1.0 + 3.0 * tension);

            return 1.0 - std::pow (1.0 - x, 1.0 - 3.0 * tension);
        }
    }

    std::vector<std::vector<size_t>> findChords (const std::vector<Note>& notes)
    {
        std::vector<size_t> order (notes.size());
        std::iota (order.begin(), order.end(), size_t (0));
        std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b)
        {
            if (std::abs (notes[a].startBeat - notes[b].startBeat) > sameTime)
                return notes[a].startBeat < notes[b].startBeat;

            return notes[a].pitch < notes[b].pitch;
        });

        std::vector<std::vector<size_t>> chords;

        for (const auto i : order)
        {
            if (chords.empty() || std::abs (notes[chords.back().front()].startBeat - notes[i].startBeat) > sameTime)
                chords.emplace_back();

            chords.back().push_back (i);
        }

        return chords;
    }

    //==============================================================================
    std::vector<Note> strum (std::vector<Note> notes, const StrumSettings& settings)
    {
        int strummed = 0;

        for (const auto& chord : findChords (notes))
        {
            const auto size = chord.size();

            if (size < 2)
                continue;

            const auto upwards = settings.direction == StrumDirection::up
                              || (settings.direction == StrumDirection::alternate && strummed % 2 == 0);
            ++strummed;

            for (size_t k = 0; k < size; ++k)
            {
                auto& n = notes[upwards ? chord[k] : chord[size - 1 - k]];
                const auto x = (double) k / (double) (size - 1);
                const auto end = n.getEndBeat();

                n.startBeat = snap::roundToTicks (n.startBeat + bend (x, settings.tension) * std::max (0.0, settings.timeBeats));

                if (settings.keepEnds)
                    n.lengthBeats = std::max (oneTick, snap::roundToTicks (end - n.startBeat));

                n.velocity = juce::jlimit (1, 127, juce::roundToInt (n.velocity * (1.0 + settings.velocity * x)));
            }
        }

        return notes;
    }

    //==============================================================================
    std::vector<Note> humanize (std::vector<Note> notes, const HumanizeSettings& settings, double minStart)
    {
        juce::Random random (settings.seed);

        for (auto& n : notes)
        {
            // Always three numbers per note, so one amount changing doesn't reshuffle the others.
            const auto timing = random.nextDouble() * 2.0 - 1.0;
            const auto velocity = random.nextDouble() * 2.0 - 1.0;
            const auto length = random.nextDouble() * 2.0 - 1.0;

            n.startBeat = snap::roundToTicks (std::max (minStart, n.startBeat + timing * settings.timingBeats));
            n.velocity = juce::jlimit (1, 127, juce::roundToInt (n.velocity + velocity * settings.velocity));
            n.lengthBeats = std::max (oneTick, snap::roundToTicks (n.lengthBeats * (1.0 + length * settings.length)));
        }

        return notes;
    }

    //==============================================================================
    std::vector<Note> scaleLevels (std::vector<Note> notes, const ScaleLevelsSettings& settings)
    {
        const auto centre = note_property::isBipolar (settings.property) ? 0.5 : 0.0;

        for (auto& n : notes)
        {
            const auto value = (double) note_property::get (n, settings.property);
            note_property::set (n, settings.property, (float) (centre + (value - centre) * settings.multiply + settings.offset));
        }

        return notes;
    }

    //==============================================================================
    Result arpeggiate (const std::vector<Note>& notes, const ArpSettings& settings)
    {
        Result result { notes, {}, {} };

        if (settings.stepBeats <= 0.0)
            return result;

        for (const auto& chord : findChords (notes))
        {
            if (chord.size() < 2)
                continue;

            const auto start = notes[chord.front()].startBeat;
            auto end = start;

            for (const auto i : chord)
                end = std::max (end, notes[i].getEndBeat());

            // (chord note, pitch) from the bottom up, octave by octave.
            std::vector<std::pair<size_t, int>> rising;

            for (int octave = 0; octave < std::max (1, settings.octaves); ++octave)
                for (const auto i : chord)
                    if (const auto pitch = notes[i].pitch + 12 * octave; pitch <= 127)
                        rising.emplace_back (i, pitch);

            auto falling = rising;
            std::reverse (falling.begin(), falling.end());

            const auto andBack = [] (std::vector<std::pair<size_t, int>> there, const std::vector<std::pair<size_t, int>>& back)
            {
                // Back again without playing the turning notes twice.
                if (back.size() > 2)
                    there.insert (there.end(), back.begin() + 1, back.end() - 1);

                return there;
            };

            std::vector<std::pair<size_t, int>> sequence;

            switch (settings.pattern)
            {
                case ArpPattern::up:     sequence = rising; break;
                case ArpPattern::down:   sequence = falling; break;
                case ArpPattern::upDown: sequence = andBack (rising, falling); break;
                case ArpPattern::downUp: sequence = andBack (falling, rising); break;
            }

            const auto gate = juce::jlimit (0.05, 1.0, settings.gate);

            for (size_t step = 0;; ++step)
            {
                const auto at = start + (double) step * settings.stepBeats;

                if (at >= end - sameTime)
                    break;

                const auto& [source, pitch] = sequence[step % sequence.size()];
                auto n = notes[source];
                n.pitch = pitch;
                n.startBeat = snap::roundToTicks (at);
                n.lengthBeats = std::max (oneTick, snap::roundToTicks (std::min (settings.stepBeats * gate, end - at)));
                result.added.push_back (n);
            }

            result.removed.insert (result.removed.end(), chord.begin(), chord.end());
        }

        std::sort (result.removed.begin(), result.removed.end());
        return result;
    }
}
