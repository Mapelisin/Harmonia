#pragma once

#include "audio/Loudness.h"

/** Mix Doctor, first version: measures the song and each of its tracks as they are heard and
    points out what usually needs fixing: clipping, stereo that falls apart in mono, much too
    much or too little bass or top, a track lost under the others. Measurements and plain rules
    only, with wide limits: it speaks up about what is clearly off, not about taste. */
namespace harmonia::audio::mix_check
{
    /** Left against right, and how the bass and the top compare with the middle. */
    struct Balance
    {
        double correlation = 1.0;   ///< 1: left and right the same (mono); 0: unrelated; below 0: opposed
        double lowDb = 0.0;         ///< the energy below lowSplitHz against the middle's, dB (0: as much)
        double highDb = 0.0;        ///< the energy above highSplitHz against the middle's, dB
    };

    inline constexpr double lowSplitHz = 120.0, highSplitHz = 6000.0;

    /** Takes sound (1 or 2 channels) a block at a time. */
    class BalanceMeter
    {
    public:
        explicit BalanceMeter (double sampleRate);

        void process (const juce::AudioBuffer<float>&, int numSamples);
        Balance getBalance() const;

    private:
        /** Two second-order filters in a row: 24 dB an octave (Linkwitz-Riley). */
        struct Steep
        {
            juce::IIRFilter first, second;

            void setCoefficients (const juce::IIRCoefficients&) noexcept;
            float process (float) noexcept;
        };

        Steep low, middleHighPass, middleLowPass, high;
        double leftRight = 0.0, leftLeft = 0.0, rightRight = 0.0, lowEnergy = 0.0, middleEnergy = 0.0, highEnergy = 0.0;
    };

    //==============================================================================
    struct TrackLevel
    {
        juce::String name;
        std::optional<loudness::Measurement> level;     ///< nothing if the track is silent
    };

    struct Report
    {
        loudness::Measurement mix;
        Balance balance;
        std::vector<TrackLevel> tracks;     ///< the tracks that are heard and have clips, loudest first
    };

    /** Renders the song, then each track (as its stem: with its effects and its sends), and
        measures them. Blocks until done (a few seconds a minute of song and track); `progress`
        hears what it's doing. Nothing, with the reason in `error`, if there's nothing to hear. */
    std::optional<Report> check (te::Edit&, juce::String& error,
                                 const std::function<void (const juce::String& step)>& progress = {});

    /** What to look at, most important first; empty if nothing stands out. */
    juce::StringArray findProblems (const Report&);

    /** The whole report, for the user: the numbers, the problems, the tracks. */
    juce::String describe (const Report&);
}
