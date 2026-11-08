#pragma once

#include "core/Session.h"

/** How loud the song is, the way streaming services measure it (integrated loudness in LUFS, by
    ITU-R BS.1770 / EBU R128), and how much to raise or lower it: the first step of mastering. */
namespace harmonia::audio::loudness
{
    struct Measurement
    {
        double lufs = -70.0;        ///< integrated loudness of the whole song
        float peakDb = -100.0f;     ///< the loudest sample, dBFS
        double seconds = 0.0;
    };

    /** Measures sound (1 or 2 channels). Nothing if it is shorter than 0.4 s or silent. */
    std::optional<Measurement> measure (const juce::AudioBuffer<float>&, double sampleRate);

    /** Measures sound handed over a block at a time (a song too long to hold in memory). */
    class Meter
    {
    public:
        Meter (double sampleRate, int numChannels);
        ~Meter();

        void process (const juce::AudioBuffer<float>&, int numSamples);

        /** What it heard so far. Nothing if that is shorter than 0.4 s or silent. */
        std::optional<Measurement> finish() const;

    private:
        struct State;
        std::unique_ptr<State> state;

        JUCE_DECLARE_NON_COPYABLE (Meter)
    };

    /** Measures a sound file, reading it a piece at a time. Nothing if it can't be read or is
        silent. */
    std::optional<Measurement> measureFile (const juce::File&);

    /** Renders the song as it is heard (as Export WAV does) and measures it. Blocks until done.
        Nothing, with the reason in `error`, if there is nothing to hear. */
    std::optional<Measurement> measureSong (te::Edit&, juce::String& error);

    /** Where streaming services (Spotify, YouTube, Apple Music...) bring songs to. */
    inline constexpr double streamingLufs = -14.0;

    /** How high peaks may go: a little under 0 dB, so encoding to MP3 / AAC doesn't clip. */
    inline constexpr float peakCeilingDb = -1.0f;

    struct Advice
    {
        float gainDb = 0.0f;        ///< what to add to the master volume (may be negative)
        bool peakLimited = false;   ///< less than the target needs: the peaks would go over the ceiling
        juce::String text;          ///< what it means, for the user
    };

    /** How to bring a measured song to `targetLufs` without its peaks going over the ceiling. */
    Advice advise (const Measurement&, double targetLufs = streamingLufs, float ceilingDb = peakCeilingDb);

    /** Adds `gainDb` to the master volume (as far as its fader goes: +6 dB at most). Returns what
        was really added. */
    float addToMasterVolume (te::Edit&, float gainDb);

    struct LimiterResult
    {
        Measurement before, after;
        float gainDb = 0.0f;        ///< the limiter's gain now
    };

    /** Brings the song to `targetLufs` with Harmonia's limiter as the master's last effect (added if
        there is none: its ceiling at -1 dB keeps the peaks under it). The master fader goes to 0 dB
        (it comes after the effects: above 0 it would push the peaks over the ceiling, below it
        would waste room). Measures the song a few times, each a render, so takes a few seconds a
        minute of song; `progress` hears what it is doing. The changes go into the undo step
        already begun. Nothing, with the reason in `error`, if there's nothing to hear. */
    std::optional<LimiterResult> raiseWithLimiter (te::Edit&, juce::String& error, double targetLufs = streamingLufs,
                                                   const std::function<void (const juce::String& step)>& progress = {});
}
