#pragma once

#include "core/Tracktion.h"

namespace harmonia::audio
{
    /** A sound played faster or slower at its own pitch (the stretcher the clips that follow the
        tempo are played through): what lets a loop be heard at the song's tempo before it is in
        the song. `speed` 2: twice as fast, half as long.

        It reads its source and stretches it when it is asked for sound, so it is to be read ahead
        of the playing on another thread (juce::AudioTransportSource does that), never by the
        audio thread itself. */
    class StretchedSource final : public juce::PositionableAudioSource
    {
    public:
        /** The speeds a sound can be played at. */
        static constexpr double minSpeed = 0.25, maxSpeed = 4.0;

        /** `source` is read from its start; `numChannels` (1 or 2) is how many it has. */
        StretchedSource (std::unique_ptr<juce::PositionableAudioSource> source, double sampleRate, int numChannels, double speed);

        double getSpeed() const noexcept        { return speed; }

        void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

        /** Positions and lengths are in what is heard (the source's, divided by the speed). */
        void setNextReadPosition (juce::int64) override;
        juce::int64 getNextReadPosition() const override    { return position; }
        juce::int64 getTotalLength() const override;
        bool isLooping() const override                     { return false; }

    private:
        static constexpr int blockSize = 512;

        const std::unique_ptr<juce::PositionableAudioSource> source;
        const int numChannels;
        const double speed;
        te::TimeStretcher stretcher;
        juce::AudioBuffer<float> input, block;
        juce::int64 position = 0;
        int blockStart = 0, blockLeft = 0;      // what of `block` hasn't been handed out yet
        int toSkip = 0;                         // what the stretcher gives before the sound itself

        void start (juce::int64 at);
        void makeBlock();

        JUCE_DECLARE_NON_COPYABLE (StretchedSource)
    };
}
