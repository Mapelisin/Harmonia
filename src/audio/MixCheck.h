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
        te::EditItemID track {};                        ///< which one it is (for the fixes)
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

    /** check a bit at a time (audio::ExportJob): its result is nothing if there was nothing to
        hear (getError then says so). Changes nothing in the song. */
    using CheckJob = ResultJob<std::optional<Report>>;
    std::unique_ptr<CheckJob> makeCheckJob (te::Edit&);

    /** What to look at, most important first; empty if nothing stands out. */
    juce::StringArray findProblems (const Report&);

    /** The whole report, for the user: the numbers, the problems, the tracks. */
    juce::String describe (const Report&);

    //==============================================================================
    /** What Mix Doctor can put right by itself, with faders: the clipping, and tracks lost under
        the others. */
    struct Fixes
    {
        bool clips = false;                                     ///< the master comes down
        std::vector<std::pair<te::EditItemID, float>> raise;    ///< tracks, and how many dB up
        juce::StringArray descriptions;                         ///< what it would do, for the user

        bool isEmpty() const noexcept       { return ! clips && raise.empty(); }
    };

    /** How far under the loudest track a lost one is brought up to. */
    inline constexpr double raisedLu = 10.0;

    /** The fixes for a report: a lost track goes up to raisedLu under the loudest (as far as its
        fader goes: +6 dB); if the song clips, the master comes down. */
    Fixes findFixes (const Report&, te::Edit&);

    /** Puts them right, into the undo step already begun: raises the tracks, then (if the song
        clipped, or the raised tracks might make it) brings the master down until the peaks are
        at -1 dB, measuring the song again (a render or a few: a clipped peak doesn't say how far
        over it went). Returns what it did, for the user ("master -2.5 dB", "\"Pad\" +7.0 dB");
        if it couldn't measure the song, says why in `error`. */
    juce::StringArray applyFixes (te::Edit&, const Fixes&, juce::String& error,
                                  const std::function<void (const juce::String& step)>& progress = {});

    /** applyFixes a bit at a time (audio::ExportJob): its result is what it did. However long it
        takes, what it changes stays in the undo step begun before it was made. Stopped half way,
        it leaves what it has changed so far in that step: take the step back
        (core::Project::takeBackGesture). */
    using FixJob = ResultJob<juce::StringArray>;
    std::unique_ptr<FixJob> makeFixJob (te::Edit&, const Fixes&);
}
