#include "core/Tempo.h"

namespace harmonia::core::tempo
{
    double clampBpm (double bpm)
    {
        return std::round (juce::jlimit (minBpm, maxBpm, bpm) * 100.0) / 100.0;
    }

    const std::vector<int>& getBeatsPerBarChoices()
    {
        static const std::vector<int> choices { 2, 3, 4, 5, 6, 7 };
        return choices;
    }

    juce::String toText (double bpm)
    {
        const auto rounded = std::round (bpm * 100.0) / 100.0;
        const auto whole = std::abs (rounded - std::round (rounded)) < 0.005;
        const auto tenths = std::abs (rounded * 10.0 - std::round (rounded * 10.0)) < 0.05;
        return juce::String (rounded, whole ? 0 : tenths ? 1 : 2) + " BPM";
    }

    std::optional<double> TapTempo::tap (double seconds)
    {
        if (! taps.empty() && (seconds - taps.back() > maxGapSeconds || seconds <= taps.back()))
            taps.clear();

        taps.push_back (seconds);

        if (taps.size() > maxIntervals + 1)
            taps.erase (taps.begin(), taps.end() - (std::ptrdiff_t) (maxIntervals + 1));

        if (taps.size() < 2)
            return {};

        const auto interval = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        const auto bpm = juce::jlimit (minBpm, maxBpm, 60.0 / interval);
        return std::round (bpm * 10.0) / 10.0;
    }
}
