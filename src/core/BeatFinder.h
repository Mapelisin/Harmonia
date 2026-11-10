#pragma once

#include <optional>
#include <vector>

/** Finds the beat of a sound from the sound itself: where its hits are, the tempo they keep, and
    where its first beat is. No engine and no files: samples in, numbers out.

    The hits are the moments the sound's loudness jumps up (in its low, middle or high part). The
    tempo is the one whose grid of sixteenths most of the hits sit on, measured over the whole
    sound, so a tempo between whole numbers (72.3) comes out as it is. A sound that is a whole
    number of beats long from its very start is taken for a loop cut to length: its tempo is then
    exact, and it repeats without a gap.

    A sound that doesn't keep one tempo (somebody playing, not a machine) has no such grid. Its
    hits are followed along the sound instead, from where they are steadiest, the grid's step
    giving way a little at each hit: what comes out is where each beat is. */
namespace harmonia::core::beat_finder
{
    inline constexpr double minBpm = 50.0, maxBpm = 250.0;

    /** A hit: when (seconds from the start of the sound), and how strong against the strongest (0..1). */
    struct Hit
    {
        double seconds = 0.0;
        float strength = 0.0f;
    };

    struct Beat
    {
        double bpm = 0.0;                   // of a sound whose tempo drifts: its tempo on the whole
        double firstBeatSeconds = 0.0;      // where the first beat of the sound is
        double certainty = 0.0;             // 0..1: how much of the hits the beat explains
        double offGridMs = 0.0;             // how far those hits are from the grid, typically
        bool wholeLoop = false;             // a whole number of beats long from its start

        /** Of a sound that doesn't keep one tempo: where each of its beats is, in seconds, from
            the first (firstBeatSeconds) to the last one heard. Empty for a sound that keeps its
            tempo: its beats are firstBeatSeconds + n * 60 / bpm. */
        std::vector<double> beatSeconds;

        bool drifts() const noexcept        { return ! beatSeconds.empty(); }
    };

    /** The hits of a sound (one channel, -1..1). */
    std::vector<Hit> findHits (const float* samples, int numSamples, double sampleRate);

    /** The beat the hits keep, if they keep one (a pad, a spoken phrase or a single hit doesn't):
        one tempo, or, when the tempo wanders by more than a player's feel, beat by beat.
        A beat twice or half as fast fits the same hits: of those, the tempo nearest `nearBpm` is
        given (the song's tempo, so that the sound is stretched least). `lengthSeconds`: how
        long the whole sound is (0: not known, so it can't be told for a loop cut to length). */
    std::optional<Beat> findBeat (const std::vector<Hit>&, double lengthSeconds, double nearBpm);

    /** Both at once. */
    std::optional<Beat> analyse (const float* samples, int numSamples, double sampleRate, double nearBpm);
}
