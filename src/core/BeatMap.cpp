#include "core/BeatMap.h"

#include <algorithm>
#include <cmath>

namespace harmonia::core
{
    namespace
    {
        using Change = BeatMap::Change;

        // Tempi this close are one tempo (a hundredth of a millisecond a beat).
        constexpr double sameTempo = 2.0e-5;

        /** The tempo from `beat` on, after those there are (the first: from the file's start).
            A tempo that is the one before it changes nothing and is left out. */
        void add (std::vector<Change>& changes, double beat, double bpm)
        {
            if (changes.empty())
            {
                changes.push_back ({ 0.0, bpm, 0.0 });
                return;
            }

            const auto& last = changes.back();

            if (std::abs (bpm - last.bpm) <= sameTempo * last.bpm)
                return;

            changes.push_back ({ beat, bpm, last.seconds + (beat - last.beat) * 60.0 / last.bpm });
        }
    }

    BeatMap::BeatMap (std::vector<Change> c)
        : changes (std::make_shared<const std::vector<Change>> (std::move (c)))
    {
    }

    BeatMap::BeatMap (double bpm)
        : BeatMap (std::vector<Change> { { 0.0, std::max (1.0, bpm), 0.0 } })
    {
    }

    std::optional<BeatMap> BeatMap::fromBeats (const std::vector<double>& beatSeconds)
    {
        if (beatSeconds.size() < 2 || beatSeconds.front() < 0.0)
            return std::nullopt;

        for (size_t i = 1; i < beatSeconds.size(); ++i)
            if (beatSeconds[i] - beatSeconds[i - 1] < 0.01)
                return std::nullopt;

        // The first beat is where the tempo up to the second puts it, counting from the file's start.
        std::vector<Change> changes;
        const auto firstBeat = beatSeconds[0] / (beatSeconds[1] - beatSeconds[0]);

        for (size_t i = 0; i + 1 < beatSeconds.size(); ++i)
            add (changes, i == 0 ? 0.0 : firstBeat + (double) i, 60.0 / (beatSeconds[i + 1] - beatSeconds[i]));

        return BeatMap (std::move (changes));
    }

    juce::String BeatMap::toString() const
    {
        juce::StringArray numbers;

        for (const auto& change : *changes)
        {
            numbers.add (juce::String (change.beat, 6));
            numbers.add (juce::String (change.bpm, 6));
        }

        return numbers.joinIntoString (" ");
    }

    std::optional<BeatMap> BeatMap::fromString (const juce::String& text)
    {
        juce::StringArray numbers;
        numbers.addTokens (text, " ", {});
        numbers.removeEmptyStrings();

        if (numbers.size() < 2 || numbers.size() % 2 != 0)
            return std::nullopt;

        std::vector<Change> changes;

        for (int i = 0; i < numbers.size(); i += 2)
        {
            const auto beat = numbers[i].getDoubleValue(), bpm = numbers[i + 1].getDoubleValue();
            const auto inOrder = changes.empty() ? beat == 0.0 : beat > changes.back().beat;

            if (! inOrder || bpm <= 0.0)
                return std::nullopt;

            // As written, every one of them: the engine reads the same numbers.
            const auto seconds = changes.empty() ? 0.0
                                                 : changes.back().seconds + (beat - changes.back().beat) * 60.0 / changes.back().bpm;
            changes.push_back ({ beat, bpm, seconds });
        }

        return BeatMap (std::move (changes));
    }

    double BeatMap::toBeats (double seconds) const noexcept
    {
        auto next = std::upper_bound (changes->begin() + 1, changes->end(), seconds,
                                      [] (double value, const Change& change) { return value < change.seconds; });
        const auto& change = *(next - 1);
        return change.beat + (seconds - change.seconds) * change.bpm / 60.0;
    }

    double BeatMap::toSeconds (double beats) const noexcept
    {
        auto next = std::upper_bound (changes->begin() + 1, changes->end(), beats,
                                      [] (double value, const Change& change) { return value < change.beat; });
        const auto& change = *(next - 1);
        return change.seconds + (beats - change.beat) * 60.0 / change.bpm;
    }

    BeatMap BeatMap::scaled (double factor) const
    {
        std::vector<Change> scaledChanges;

        for (const auto& change : *changes)
            scaledChanges.push_back ({ change.beat * factor, change.bpm * factor, change.seconds });

        return BeatMap (std::move (scaledChanges));
    }

    std::optional<BeatMap> BeatMap::withBeatAt (double beat, double seconds, double span) const
    {
        constexpr double same = 1.0e-9;

        if (beat <= same || span <= same)
            return std::nullopt;

        const auto lower = std::max (0.0, beat - span), upper = beat + span;
        const auto lowerSeconds = toSeconds (lower), upperSeconds = toSeconds (upper);

        // Where it may be depends on its neighbours alone: however it is dragged, the tempo
        // either side of it is never more than four times what an even one would be.
        const auto between = upperSeconds - lowerSeconds;
        const auto share = (beat - lower) / (upper - lower);
        const auto at = std::clamp (seconds, lowerSeconds + 0.25 * share * between, upperSeconds - 0.25 * (1.0 - share) * between);

        // The tempo in effect at a beat, as it was.
        const auto tempoAt = [this] (double b)
        {
            auto next = std::upper_bound (changes->begin() + 1, changes->end(), b + same,
                                          [] (double value, const Change& change) { return value < change.beat; });
            return (next - 1)->bpm;
        };

        std::vector<Change> moved;

        for (const auto& change : *changes)
            if (change.beat < lower - same)
                add (moved, change.beat, change.bpm);

        add (moved, lower, 60.0 * (beat - lower) / (at - lowerSeconds));
        add (moved, beat, 60.0 * (upper - beat) / (upperSeconds - at));
        add (moved, upper, tempoAt (upper));

        for (const auto& change : *changes)
            if (change.beat > upper + same)
                add (moved, change.beat, change.bpm);

        return BeatMap (std::move (moved));
    }

    std::optional<BeatMap> BeatMap::withoutChangeAt (double beat) const
    {
        const auto found = std::find_if (changes->begin() + 1, changes->end(),
                                         [beat] (const Change& change) { return std::abs (change.beat - beat) < 1.0e-6; });

        if (found == changes->end())
            return std::nullopt;

        std::vector<Change> without;

        for (auto change = changes->begin(); change != changes->end(); ++change)
        {
            if (change == found)
                continue;

            // The change before it takes the sound to the one after it (the last one just goes on).
            if (change + 1 == found && found + 1 != changes->end())
                add (without, change->beat, 60.0 * ((found + 1)->beat - change->beat) / ((found + 1)->seconds - change->seconds));
            else
                add (without, change->beat, change->bpm);
        }

        return BeatMap (std::move (without));
    }

    BeatMap BeatMap::reversed (double fileSeconds) const
    {
        // What was the last stretch of the file is the first now; a change past the end of the
        // file is no part of it.
        const auto allBeats = toBeats (fileSeconds);
        std::vector<Change> turned;
        auto end = allBeats;        // where the stretch being added ends in the file as it was

        for (auto change = changes->rbegin(); change != changes->rend(); ++change)
        {
            if (change->beat >= allBeats && change->beat > 0.0)
                continue;

            add (turned, allBeats - end, change->bpm);
            end = change->beat;
        }

        return BeatMap (std::move (turned));
    }
}
