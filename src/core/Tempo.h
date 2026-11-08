#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

/** The song's tempo and time signature, as the user sets them (see Project::setTempoBpm and
    Project::setBeatsPerBar), and tapping the tempo. */
namespace harmonia::core::tempo
{
    /** The engine's limits. */
    inline constexpr double minBpm = 20.0, maxBpm = 300.0;

    /** A tempo as typed or dragged: within the limits, to 0.01 BPM. */
    double clampBpm (double bpm);

    /** Beats per bar offered for time signatures (2/4 ... 7/4). Beats are quarter notes
        everywhere in Harmonia, so signatures are over 4. */
    const std::vector<int>& getBeatsPerBarChoices();

    /** "120 BPM", "92.5 BPM". */
    juce::String toText (double bpm);

    /** Tap tempo: the tempo of the last few taps. */
    class TapTempo
    {
    public:
        /** A tap at `seconds` (any steady clock). From the second tap on, returns the tempo: the
            average of the last intervals (up to 4), rounded to 0.1 BPM, within the limits. A pause
            longer than `maxGapSeconds` starts over. */
        std::optional<double> tap (double seconds);

        void reset()                        { taps.clear(); }
        int getNumTaps() const noexcept     { return (int) taps.size(); }

        static constexpr double maxGapSeconds = 2.0;
        static constexpr size_t maxIntervals = 4;

    private:
        std::vector<double> taps;
    };
}
