#include "core/BeatFollower.h"
#include "core/BeatGrid.h"

#include <algorithm>
#include <cmath>

namespace harmonia::core::beat_finder
{
    using namespace grid;

    namespace
    {
        /** A hit taken for a place in the grid of sixteenths. */
        struct Match
        {
            int slot = 0;
            double seconds = 0.0;
            float strength = 0.0f;
        };

        /** The eight beats of the sound whose hits keep a grid best: where following starts. */
        std::optional<Grid> steadiestStretch (const std::vector<Hit>& hits, double period)
        {
            const auto length = 8.0 * period, hop = 2.0 * period;
            std::optional<Grid> best;
            auto bestCertainty = 0.0;
            std::vector<Hit> inside;

            for (auto from = hits.front().seconds; ; from += hop)
            {
                inside.clear();

                for (const auto& hit : hits)
                    if (hit.seconds >= from && hit.seconds < from + length)
                        inside.push_back (hit);

                if ((int) inside.size() >= fewestHits)
                {
                    // The tempo there may be well off the sound's tempo on the whole.
                    auto grid = bestGrid (inside, period, 0.1, 0.001);
                    auto off = 0.0;
                    const auto certainty = refine (inside, grid, off);

                    // Later stretches have to be clearly better: the sound is followed both ways
                    // from here, and the start of a sound is where its first beat is looked for.
                    if (grid.step > 0.0 && certainty >= leastCertainty && certainty > bestCertainty + 0.05)
                    {
                        bestCertainty = certainty;
                        best = grid;
                    }
                }

                if (from + length >= hits.back().seconds)
                    break;
            }

            return best;
        }

        // How far the tempo may be off over a stretch with nothing to hear it by.
        constexpr double tempoGivesWay = 0.03;

        // The grid is followed from hit to hit no further than this (two beats): a hit after a
        // longer gap is near some sixteenth whatever the tempo did meanwhile.
        constexpr int farthestSlots = 8;

        /** Follows the hits one way along the sound from where `grid` holds (later: 1, earlier: -1).
            A hit near where the grid says the next sixteenths are is on it; the grid then moves
            most of the way to the hit, and its step a little.

            After a break (nothing on the grid for two beats: silence, or the tempo gone elsewhere)
            the grid is found again in the hits that come next. How many sixteenths the break was is
            counted by the tempo either side of it; where that leaves it open, a clear hit is taken
            to come in on a beat (the sixteenths are numbered from the grid's start, a strong hit). */
        void follow (const std::vector<Hit>& hits, Grid grid, int direction, std::vector<Match>& matches)
        {
            // The hits on the way, nearest first.
            std::vector<Hit> ahead;

            for (const auto& hit : hits)
                if ((hit.seconds >= grid.start - onGridSeconds) == (direction > 0))
                    ahead.push_back (hit);

            if (direction < 0)
                std::reverse (ahead.begin(), ahead.end());

            auto at = grid.start;       // where the grid's sixteenth number `slot` is
            auto step = grid.step;
            int slot = 0;
            auto lastTaken = grid.start;

            const auto take = [&] (const Hit& hit)
            {
                const auto gap = (hit.seconds - at) * direction;
                const auto slots = (int) std::round (gap / step);

                if (slots < 0 || slots > farthestSlots)
                    return false;

                // The further from the last hit, the less sure where the grid is by now.
                const auto off = gap - slots * step;
                const auto tolerance = std::min (0.35 * step, onGridSeconds + tempoGivesWay * slots * step);

                if (std::abs (off) > tolerance)
                    return false;

                const auto trust = 0.3 + 0.7 * (double) hit.strength;
                at += direction * (slots * step + 0.7 * trust * off);
                slot += direction * slots;

                if (slots > 0)
                    step = std::clamp (step + 0.3 * trust * off / slots, grid.step * 0.75, grid.step * 1.33);

                matches.push_back ({ slot, hit.seconds, hit.strength });
                lastTaken = hit.seconds;
                return true;
            };

            std::vector<Hit> next;

            for (size_t i = 0; i < ahead.size(); ++i)
            {
                const auto& hit = ahead[i];

                if (take (hit) || std::abs (hit.seconds - lastTaken) <= farthestSlots * step)
                    continue;

                // Lost. Found again or not, the grid isn't looked for again before two more beats.
                lastTaken = hit.seconds;
                next.clear();

                for (auto j = i; j < ahead.size() && std::abs (ahead[j].seconds - hit.seconds) < 32.0 * step; ++j)
                    next.push_back (ahead[j]);

                if ((int) next.size() < fewestHits)
                    continue;

                auto found = bestGrid (next, 4.0 * step, 0.1, 0.001);
                auto off = 0.0;

                if (refine (next, found, off) < leastCertainty || found.step <= 0.0)
                    continue;

                // The sixteenth of the grid found that this hit is at, and which one it is, counting on.
                const auto first = found.start + std::round ((hit.seconds - found.start) / found.step) * found.step;
                const auto counted = (first - at) * direction / (0.5 * (step + found.step));
                auto number = slot + direction * (int) std::round (counted);

                if (hit.strength >= 0.3f && std::abs (hit.seconds - first) <= onGridSeconds)
                {
                    const auto guess = slot + direction * counted;
                    const auto onBeat = (int) std::round (guess / 4.0) * 4;

                    if (std::abs (onBeat - guess) <= tempoGivesWay * counted)
                        number = onBeat;
                }

                if ((number - slot) * direction < 1)
                    continue;

                at = first;
                slot = number;
                step = std::clamp (found.step, grid.step * 0.75, grid.step * 1.33);
                take (hit);
            }
        }

        /** One hit for each sixteenth that has any (the strongest), in order. */
        std::vector<Match> onePerSlot (std::vector<Match> matches)
        {
            std::sort (matches.begin(), matches.end(), [] (const Match& a, const Match& b)
            {
                return a.slot != b.slot ? a.slot < b.slot : a.strength > b.strength;
            });

            std::vector<Match> result;

            for (const auto& match : matches)
                if (result.empty() || (match.slot > result.back().slot && match.seconds > result.back().seconds))
                    result.push_back (match);

            return result;
        }

        /** Which beat of `beats` a moment is at (2.5: half way from the third to the fourth); before
            the first and after the last as the nearest two go on. */
        double beatAt (const std::vector<double>& beats, double seconds)
        {
            const auto next = std::upper_bound (beats.begin() + 1, beats.end() - 1, seconds);
            const auto index = (size_t) (next - beats.begin());
            return (double) (index - 1) + (seconds - beats[index - 1]) / (beats[index] - beats[index - 1]);
        }
    }

    //==============================================================================
    std::optional<Beat> followBeat (const std::vector<Hit>& hits, double period)
    {
        const auto start = steadiestStretch (hits, period);

        if (! start.has_value())
            return std::nullopt;

        std::vector<Match> found;
        follow (hits, *start, 1, found);
        follow (hits, *start, -1, found);
        const auto matches = onePerSlot (std::move (found));

        if ((int) matches.size() < fewestHits)
            return std::nullopt;

        // The first beat: the first clear hit that is followed. The others: every fourth
        // sixteenth from it, where its hit is, or between the hits either side of it.
        size_t first = 0;

        while (first + 1 < matches.size() && matches[first].strength < 0.3f)
            ++first;

        Beat beat;
        size_t next = first;

        for (auto slot = matches[first].slot; slot <= matches.back().slot; slot += 4)
        {
            while (matches[next].slot < slot)
                ++next;

            if (matches[next].slot == slot)
            {
                beat.beatSeconds.push_back (matches[next].seconds);
            }
            else
            {
                const auto& before = matches[next - 1];
                const auto& after = matches[next];
                beat.beatSeconds.push_back (before.seconds + (after.seconds - before.seconds) * (slot - before.slot)
                                                                 / (double) (after.slot - before.slot));
            }
        }

        // A rhythm: beats enough, none absurdly long or short.
        const auto numBeats = beat.beatSeconds.size();

        if (numBeats < 3)
            return std::nullopt;

        for (size_t i = 1; i < numBeats; ++i)
        {
            const auto gap = beat.beatSeconds[i] - beat.beatSeconds[i - 1];

            if (gap < 0.6 * period || gap > 1.6 * period)
                return std::nullopt;
        }

        beat.firstBeatSeconds = beat.beatSeconds.front();
        beat.bpm = 60.0 * (double) (numBeats - 1) / (beat.beatSeconds.back() - beat.beatSeconds.front());

        if (beat.bpm < minBpm - 0.5 || beat.bpm > maxBpm + 0.5)
            return std::nullopt;

        // How much of the hits are on the sixteenths between those beats, and how near.
        double all = 0.0, on = 0.0, squares = 0.0;

        for (const auto& hit : hits)
        {
            all += hit.strength;
            const auto at = beatAt (beat.beatSeconds, hit.seconds);
            const auto index = (size_t) std::clamp (std::floor (at), 0.0, (double) (numBeats - 2));
            const auto beatLength = beat.beatSeconds[index + 1] - beat.beatSeconds[index];
            const auto off = offGrid (at, 0.25) * beatLength;

            if (std::abs (off) <= std::min (onGridSeconds, beatLength * 0.075))
            {
                on += hit.strength;
                squares += hit.strength * off * off;
            }
        }

        beat.certainty = all > 0.0 ? on / all : 0.0;
        beat.offGridMs = on > 0.0 ? std::sqrt (squares / on) * 1000.0 : 0.0;

        if (beat.certainty < leastCertainty)
            return std::nullopt;

        return beat;
    }

    bool wanders (const Beat& followed, const Beat& steady)
    {
        constexpr size_t bar = 4;
        const auto& beats = followed.beatSeconds;

        if (beats.size() < 2 * bar || followed.certainty < steady.certainty - 0.02)
            return false;

        const auto beatSeconds = 60.0 / steady.bpm;
        std::vector<double> off;

        for (const auto seconds : beats)
            off.push_back (offGrid (seconds - steady.firstBeatSeconds, beatSeconds));

        auto least = 1.0e9, most = -1.0e9;

        for (size_t i = 0; i + bar <= off.size(); ++i)
        {
            auto sum = 0.0;

            for (size_t k = 0; k < bar; ++k)
                sum += off[i + k];

            least = std::min (least, sum / bar);
            most = std::max (most, sum / bar);
        }

        return most - least > 0.010;
    }
}
