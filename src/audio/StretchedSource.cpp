#include "audio/StretchedSource.h"

namespace harmonia::audio
{
    StretchedSource::StretchedSource (std::unique_ptr<juce::PositionableAudioSource> s, double sampleRate, int channels,
                                      double speedToPlayAt)
        : source (std::move (s)),
          numChannels (juce::jlimit (1, 2, channels)),
          speed (juce::jlimit (minSpeed, maxSpeed, speedToPlayAt))
    {
        jassert (source != nullptr);

        // Each block heard takes blockSize * speed of the source (a little more or less now and
        // then: the stretcher keeps the fractions).
        input.setSize (numChannels, (int) std::ceil (blockSize * speed * 1.5) + 16);
        block.setSize (numChannels, blockSize);

        stretcher.initialise (sampleRate, blockSize, numChannels, te::TimeStretcher::defaultMode, {}, false);
        start (0);
    }

    void StretchedSource::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
    {
        source->prepareToPlay (samplesPerBlockExpected, sampleRate);
    }

    void StretchedSource::releaseResources()
    {
        source->releaseResources();
    }

    juce::int64 StretchedSource::getTotalLength() const
    {
        return (juce::int64) std::ceil ((double) source->getTotalLength() / speed);
    }

    void StretchedSource::setNextReadPosition (juce::int64 newPosition)
    {
        if (newPosition != position)
            start (juce::jmax ((juce::int64) 0, newPosition));
    }

    void StretchedSource::start (juce::int64 at)
    {
        position = at;
        source->setNextReadPosition ((juce::int64) std::llround ((double) at * speed));

        // The stretcher's speed is the other way round: 2 is twice as long.
        stretcher.reset();
        stretcher.setSpeedAndPitch ((float) (1.0 / speed), 0.0f);

        // What comes out first is the stretcher warming up: the sound itself comes after what it
        // holds back of its output, and after it has taken in what it looks ahead in its input.
        const auto held = stretcher.getOutputLatencySamples();
        const auto ahead = stretcher.getLatencySamples() - held;
        toSkip = held + (int) std::llround ((double) ahead / speed);
        blockStart = blockLeft = 0;
    }

    void StretchedSource::makeBlock()
    {
        const auto needed = juce::jlimit (0, input.getNumSamples(), stretcher.getFramesNeeded());

        // Past its end the source gives silence: what the stretcher still holds comes out.
        if (needed > 0)
            source->getNextAudioBlock (juce::AudioSourceChannelInfo (&input, 0, needed));

        const float* ins[] { input.getReadPointer (0), numChannels > 1 ? input.getReadPointer (1) : nullptr, nullptr };
        float* outs[] { block.getWritePointer (0), numChannels > 1 ? block.getWritePointer (1) : nullptr, nullptr };
        stretcher.processData (ins, needed, outs);

        const auto skipped = juce::jmin (toSkip, blockSize);
        toSkip -= skipped;
        blockStart = skipped;
        blockLeft = blockSize - skipped;
    }

    void StretchedSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
    {
        if (info.buffer == nullptr || info.numSamples <= 0)
            return;

        for (int done = 0; done < info.numSamples;)
        {
            while (blockLeft == 0)
                makeBlock();

            const auto count = juce::jmin (blockLeft, info.numSamples - done);

            for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
                info.buffer->copyFrom (channel, info.startSample + done, block, juce::jmin (channel, numChannels - 1), blockStart, count);

            done += count;
            blockStart += count;
            blockLeft -= count;
        }

        position += info.numSamples;
    }
}
