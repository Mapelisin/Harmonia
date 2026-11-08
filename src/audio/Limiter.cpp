#include "audio/Limiter.h"

#include <numeric>

namespace harmonia::audio::limiter
{
    // How it works: each sample's peak (all channels) says what gain would bring it under the
    // ceiling. The lowest of those over the look-ahead window is held (so the gain is already down
    // when the peak comes out of the delay), let back up at the release speed, and averaged over
    // the window (so it goes down smoothly). The average of values that are all at most what the
    // peak needs is at most that: the peak comes out under the ceiling.
    void Processor::prepare (double rate, int maxChannels)
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        length = std::max (2, juce::roundToInt (sampleRate * lookaheadSeconds));
        delayed.setSize (std::max (1, maxChannels), length);
        wanted.assign ((size_t) length, 1.0f);
        smoothing.assign ((size_t) length, 1.0);
        reset();
    }

    void Processor::reset()
    {
        delayed.clear();
        std::fill (wanted.begin(), wanted.end(), 1.0f);
        std::fill (smoothing.begin(), smoothing.end(), 1.0);
        smoothingSum = (double) length;
        released = 1.0;
        position = 0;
        reductionDb = 0.0f;
    }

    void Processor::process (float* const* channels, int numChannels, int numSamples, float gainDb, float ceilingDb,
                             float releaseMs)
    {
        numChannels = std::min (numChannels, delayed.getNumChannels());

        if (numChannels <= 0 || wanted.empty())
            return;

        const auto gain = juce::Decibels::decibelsToGain (gainDb);
        const auto ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        const auto releaseCoefficient = 1.0 - std::exp (-1.0 / (std::max (1.0f, releaseMs) * 0.001 * sampleRate));
        const auto delay = length - 1;
        float smoothed = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;

            for (int c = 0; c < numChannels; ++c)
                peak = std::max (peak, std::abs (channels[c][i] * gain));

            wanted[(size_t) position] = peak > ceiling ? ceiling / peak : 1.0f;
            const auto held = (double) *std::min_element (wanted.begin(), wanted.end());
            released = held < released ? held : released + (held - released) * releaseCoefficient;

            smoothingSum += released - smoothing[(size_t) position];
            smoothing[(size_t) position] = released;
            smoothed = (float) std::min (1.0, smoothingSum / length);

            // Out comes the sample from `delay` samples ago, turned down as it needs.
            const auto out = (position + length - delay) % length;

            for (int c = 0; c < numChannels; ++c)
            {
                auto* line = delayed.getWritePointer (c);
                const auto past = line[out];
                line[position] = channels[c][i] * gain;
                channels[c][i] = juce::jlimit (-ceiling, ceiling, past * smoothed);  // only rounding errors are cut
            }

            position = (position + 1) % length;

            // Now and then, sum again from scratch: rounding errors add up over hours of sound.
            if (position == 0)
                smoothingSum = std::accumulate (smoothing.begin(), smoothing.end(), 0.0);
        }

        reductionDb = juce::Decibels::gainToDecibels (smoothed);
    }

    //==============================================================================
    const char* Plugin::xmlTypeName = "harmoniaLimiter";

    Plugin::Plugin (te::PluginCreationInfo info)
        : te::Plugin (info)
    {
        auto* undoManager = getUndoManager();
        gainValue.referTo (state, "gain", undoManager, 0.0f);
        ceilingValue.referTo (state, "ceiling", undoManager, defaultCeilingDb);
        releaseValue.referTo (state, "release", undoManager, defaultReleaseMs);

        const auto db = [] (float value)                    { return (value > 0.05f ? "+" : "") + juce::String (value, 1) + " dB"; };
        const auto number = [] (const juce::String& text)   { return text.getFloatValue(); };

        gain = addParam ("gain", "Gain", { 0.0f, maxGainDb }, db, number);
        ceiling = addParam ("ceiling", "Ceiling", { -12.0f, 0.0f }, db, number);
        release = addParam ("release", "Release", { 10.0f, 1000.0f, 0.0f, 0.4f },
                            [] (float value) { return juce::String (juce::roundToInt (value)) + " ms"; }, number);

        gain->attachToCurrentValue (gainValue);
        ceiling->attachToCurrentValue (ceilingValue);
        release->attachToCurrentValue (releaseValue);
    }

    Plugin::~Plugin()
    {
        notifyListenersOfDeletion();

        gain->detachFromCurrentValue();
        ceiling->detachFromCurrentValue();
        release->detachFromCurrentValue();
    }

    void Plugin::initialise (const te::PluginInitialisationInfo& info)
    {
        sampleRate = info.sampleRate;
        processor.prepare (info.sampleRate, 2);
    }

    void Plugin::reset()
    {
        processor.reset();
    }

    double Plugin::getLatencySeconds()
    {
        return std::max (1, juce::roundToInt (sampleRate * lookaheadSeconds) - 1) / sampleRate;
    }

    double Plugin::getTailLength() const
    {
        return lookaheadSeconds;
    }

    void Plugin::applyToBuffer (const te::PluginRenderContext& context)
    {
        if (context.destBuffer == nullptr)
            return;

        auto& buffer = *context.destBuffer;
        const auto numChannels = std::min (2, buffer.getNumChannels());
        float* channels[2] = {};

        for (int c = 0; c < numChannels; ++c)
            channels[c] = buffer.getWritePointer (c, context.bufferStartSample);

        processor.process (channels, numChannels, context.bufferNumSamples, gain->getCurrentValue(),
                           ceiling->getCurrentValue(), release->getCurrentValue());
    }

    void registerType (te::Engine& engine)
    {
        engine.getPluginManager().createBuiltInType<Plugin>();
    }
}
