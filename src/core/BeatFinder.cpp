#include "core/BeatFinder.h"
#include "core/BeatFollower.h"
#include "core/BeatGrid.h"

#include <algorithm>
#include <cmath>

namespace harmonia::core::beat_finder
{
    using namespace grid;

    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        // Loudness is looked at this often, in three parts of the sound.
        constexpr double framesPerSecond = 400.0;
        constexpr double lowHz = 150.0, highHz = 2500.0;

        /** The beat is looked for in what the hits are apart from each other, up to this far. */
        constexpr double longestGapSeconds = 5.0;
        constexpr double gapBinSeconds = 0.002;

        float onePole (double hz, double sampleRate)
        {
            return (float) (1.0 - std::exp (-2.0 * pi * hz / sampleRate));
        }

        /** A beat period (seconds) the hits repeat with, to within a percent or so: the one that
            explains best how far apart they are, at whatever level of the rhythm it is. */
        double roughPeriod (const std::vector<Hit>& hits)
        {
            const auto numBins = (int) (longestGapSeconds / gapBinSeconds) + 4;
            std::vector<double> gaps ((size_t) numBins, 0.0);

            for (size_t i = 0; i < hits.size(); ++i)
            {
                for (size_t j = i + 1; j < hits.size(); ++j)
                {
                    const auto gap = hits[j].seconds - hits[i].seconds;

                    if (gap >= longestGapSeconds)
                        break;

                    // Spread a little: hits are a millisecond or two off.
                    const auto bin = gap / gapBinSeconds;
                    const auto weight = (double) hits[i].strength * hits[j].strength;

                    for (int k = -3; k <= 3; ++k)
                    {
                        const auto at = (int) std::round (bin) + k;

                        if (at >= 0 && at < numBins)
                            gaps[(size_t) at] += weight * (1.0 - std::abs (k) / 4.0);
                    }
                }
            }

            const auto at = [&gaps, numBins] (double seconds)
            {
                const auto bin = seconds / gapBinSeconds;
                const auto index = (int) std::floor (bin);

                if (index < 0 || index + 1 >= numBins)
                    return 0.0;

                const auto t = bin - index;
                return gaps[(size_t) index] * (1.0 - t) + gaps[(size_t) index + 1] * t;
            };

            auto best = 0.0, bestScore = -1.0;

            for (auto period = 60.0 / maxBpm; period <= 60.0 / minBpm; period += gapBinSeconds * 0.5)
            {
                // A beat shows at its own length, and at two, three and four of them.
                const auto score = at (period) + 0.8 * at (2.0 * period) + 0.6 * at (3.0 * period) + 0.5 * at (4.0 * period);

                if (score > bestScore)
                {
                    bestScore = score;
                    best = period;
                }
            }

            return bestScore > 0.0 ? best : 0.0;
        }

        /** Of a period and its doubles and halves, the one whose tempo is nearest `nearBpm`. */
        double nearestOctave (double period, double nearBpm)
        {
            const auto wanted = 60.0 / std::clamp (nearBpm, minBpm, maxBpm);
            auto best = 0.0, bestDistance = 1.0e9;

            for (const auto factor : { 0.25, 0.5, 1.0, 2.0, 4.0 })
            {
                const auto candidate = period * factor;

                if (candidate < 60.0 / maxBpm - 1.0e-9 || candidate > 60.0 / minBpm + 1.0e-9)
                    continue;

                const auto distance = std::abs (std::log (candidate / wanted));

                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
            }

            return best;
        }

        /** The beat of a sound that keeps its tempo: one grid for all of it. `period`: the beat
            it has roughly, in seconds. */
        std::optional<Beat> steadyBeat (const std::vector<Hit>& hits, double period, double lengthSeconds)
        {
            auto grid = bestGrid (hits, period);
            auto offSeconds = 0.0;
            const auto certainty = refine (hits, grid, offSeconds);

            if (grid.step <= 0.0 || certainty < leastCertainty)
                return std::nullopt;

            // The hits on the grid must be a rhythm, not two or three that happen to fit.
            int onGrid = 0;
            auto first = 1.0e9, last = -1.0e9;

            for (const auto& hit : hits)
            {
                if (std::abs (offGrid (hit.seconds - grid.start, grid.step)) <= std::min (onGridSeconds, grid.step * 0.3))
                {
                    ++onGrid;
                    first = std::min (first, hit.seconds);
                    last = std::max (last, hit.seconds);
                }
            }

            const auto beatSeconds = grid.step * 4.0;

            if (onGrid < fewestHits || last - first < 1.5 * beatSeconds)
                return std::nullopt;

            Beat beat;
            beat.bpm = 60.0 / beatSeconds;
            beat.certainty = certainty;
            beat.offGridMs = offSeconds * 1000.0;

            if (beat.bpm < minBpm - 0.5 || beat.bpm > maxBpm + 0.5)
                return std::nullopt;

            // The first beat: where the first clear hit is, on the grid.
            auto firstClear = hits.front().seconds;

            for (const auto& hit : hits)
            {
                if (hit.strength >= 0.3f && std::abs (offGrid (hit.seconds - grid.start, grid.step)) <= onGridSeconds)
                {
                    firstClear = hit.seconds;
                    break;
                }
            }

            beat.firstBeatSeconds = std::max (0.0, grid.start + std::round ((firstClear - grid.start) / grid.step) * grid.step);

            // A loop cut to length: it begins on its first beat and is a whole number of beats long.
            const auto beatsLong = lengthSeconds / beatSeconds;
            const auto wholeBeats = std::round (beatsLong);

            if (lengthSeconds > 0.0 && beat.firstBeatSeconds < 0.02 && wholeBeats >= 2.0 && std::abs (beatsLong - wholeBeats) < 0.02)
            {
                beat.wholeLoop = true;
                beat.bpm = wholeBeats * 60.0 / lengthSeconds;
                beat.firstBeatSeconds = 0.0;
            }

            return beat;
        }
    }

    //==============================================================================
    std::vector<Hit> findHits (const float* samples, int numSamples, double sampleRate)
    {
        std::vector<Hit> hits;

        if (samples == nullptr || sampleRate <= 0.0)
            return hits;

        const auto hop = std::max (1, (int) std::round (sampleRate / framesPerSecond));
        const auto numFrames = numSamples / hop;

        if (numFrames < 16)
            return hits;

        // How loud the low, the middle and the high part of the sound are, frame by frame.
        constexpr int numBands = 3;
        std::vector<float> loudness ((size_t) numFrames * numBands);
        const auto lowCoefficient = onePole (lowHz, sampleRate), highCoefficient = onePole (highHz, sampleRate);
        float low = 0.0f, lowAndMiddle = 0.0f;
        auto loudest = 0.0;

        // Loudness falls slowly: the swing of a low note (a kick's 50 Hz is louder and quieter a
        // hundred times a second) isn't a row of hits.
        const auto fall = std::exp (-1.0 / (0.04 * framesPerSecond));
        double held[numBands] = {};

        for (int frame = 0; frame < numFrames; ++frame)
        {
            double sums[numBands] = {};

            for (int i = 0; i < hop; ++i)
            {
                const auto sample = samples[frame * hop + i];
                low += lowCoefficient * (sample - low);
                lowAndMiddle += highCoefficient * (sample - lowAndMiddle);
                const float parts[numBands] = { low, lowAndMiddle - low, sample - lowAndMiddle };

                for (int band = 0; band < numBands; ++band)
                    sums[band] += (double) parts[band] * parts[band];
            }

            for (int band = 0; band < numBands; ++band)
            {
                held[band] = std::max (sums[band] / hop, held[band] * fall);
                loudness[(size_t) (frame * numBands + band)] = (float) held[band];
                loudest = std::max (loudest, held[band]);
            }
        }

        if (loudest < 1.0e-10)
            return hits;

        // As the ear takes it (a quiet sound doubling counts like a loud one doubling)...
        for (auto& value : loudness)
            value = (float) std::log1p (1000.0 * value / loudest);

        // ...and how much it jumped up within the last frames, all parts together. Before the
        // sound there was silence: a loop cut to length begins with a hit.
        std::vector<float> rise ((size_t) numFrames, 0.0f);
        auto biggestRise = 0.0f;

        const auto loudnessAt = [&loudness] (int frame, int band)
        {
            return frame >= 0 ? loudness[(size_t) (frame * numBands + band)] : 0.0f;
        };

        for (int frame = 0; frame < numFrames; ++frame)
        {
            auto sum = 0.0f;

            for (int band = 0; band < numBands; ++band)
                sum += std::max (0.0f, loudnessAt (frame, band) - std::max (loudnessAt (frame - 1, band), loudnessAt (frame - 2, band)));

            rise[(size_t) frame] = sum;
            biggestRise = std::max (biggestRise, sum);
        }

        if (biggestRise <= 0.0f)
            return hits;

        // A hit: the biggest jump within 30 ms either way, clearly above what is usual around it.
        const auto reach = (int) (0.03 * framesPerSecond);
        const auto around = (int) (0.25 * framesPerSecond);
        const auto blockSize = std::max (1, (int) (sampleRate / 2000.0));       // half a millisecond

        for (int frame = 0; frame < numFrames; ++frame)
        {
            const auto value = rise[(size_t) frame];

            if (value < 0.08f * biggestRise)
                continue;

            auto isBiggest = true;

            for (int other = std::max (0, frame - reach); other <= std::min (numFrames - 1, frame + reach) && isBiggest; ++other)
                isBiggest = rise[(size_t) other] < value || other == frame || (rise[(size_t) other] == value && other > frame);

            if (! isBiggest)
                continue;

            auto usual = 0.0;
            const auto from = std::max (0, frame - around), to = std::min (numFrames - 1, frame + around);

            for (int other = from; other <= to; ++other)
                usual += rise[(size_t) other];

            usual /= (to - from + 1);

            if (value < 2.0 * usual)
                continue;

            // Where exactly the sound starts to rise: the first half millisecond that is well on
            // its way from how loud it was just before to how loud the hit gets.
            const auto blockPeak = [samples, numSamples, blockSize] (int start)
            {
                auto peak = 0.0f;

                for (int i = std::max (0, start); i < std::min (numSamples, start + blockSize); ++i)
                    peak = std::max (peak, std::abs (samples[i]));

                return peak;
            };

            const auto searchFrom = (frame - 2) * hop, searchTo = std::min (numSamples, (frame + 3) * hop);
            auto before = 0.0f, top = 0.0f;

            for (int start = searchFrom - 4 * hop; start < searchFrom; start += blockSize)
                before = std::max (before, blockPeak (start));

            for (int start = searchFrom; start < searchTo; start += blockSize)
                top = std::max (top, blockPeak (start));

            auto startsAt = frame * hop;

            if (top > before)
            {
                const auto level = before + 0.25f * (top - before);

                for (int start = searchFrom; start < searchTo; start += blockSize)
                {
                    if (blockPeak (start) >= level)
                    {
                        startsAt = std::max (0, start);
                        break;
                    }
                }
            }

            hits.push_back ({ startsAt / sampleRate, value / biggestRise });
        }

        return hits;
    }

    std::optional<Beat> findBeat (const std::vector<Hit>& hits, double lengthSeconds, double nearBpm)
    {
        if ((int) hits.size() < fewestHits)
            return std::nullopt;

        const auto rough = roughPeriod (hits);

        if (rough <= 0.0)
            return std::nullopt;

        const auto period = nearestOctave (rough, nearBpm);

        if (period <= 0.0)
            return std::nullopt;

        auto steady = steadyBeat (hits, period, lengthSeconds);

        // A loop cut to length is as steady as it gets.
        if (steady.has_value() && steady->wholeLoop)
            return steady;

        auto followed = followBeat (hits, period);

        if (followed.has_value() && (! steady.has_value() || wanders (*followed, *steady)))
            return followed;

        return steady;
    }

    std::optional<Beat> analyse (const float* samples, int numSamples, double sampleRate, double nearBpm)
    {
        if (sampleRate <= 0.0 || numSamples <= 0)
            return std::nullopt;

        return findBeat (findHits (samples, numSamples, sampleRate), numSamples / sampleRate, nearBpm);
    }
}
