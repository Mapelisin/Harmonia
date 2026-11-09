#include "audio/Pump.h"

namespace harmonia::audio::pump
{
    namespace
    {
        // The duck comes in over about a millisecond and a half: sudden, but no click.
        constexpr double smoothingSeconds = 0.0015;
    }

    const std::vector<Rate>& getRates()
    {
        static const std::vector<Rate> all { Rate::eighth, Rate::quarter, Rate::half, Rate::bar };
        return all;
    }

    juce::String getName (Rate rate)
    {
        switch (rate)
        {
            case Rate::eighth:  return "1/8";
            case Rate::quarter: return "1/4";
            case Rate::half:    return "1/2";
            case Rate::bar:     return "1 bar";
        }

        return {};
    }

    float gainAt (double phase, float depth, float length)
    {
        depth = juce::jlimit (0.0f, 1.0f, depth);
        length = juce::jlimit (0.01f, 1.0f, length);

        if (phase < 0.0 || phase >= length)
            return 1.0f;

        // Held low at first, then swelling back (smoothstep): the "breathing" of a sidechained pad.
        const auto t = (float) (phase / length);
        const auto swell = t * t * (3.0f - 2.0f * t);
        return 1.0f - depth * (1.0f - swell);
    }

    //==============================================================================
    void Processor::prepare (double rate)
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        smoothing = (float) (1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate)));
        reset();
    }

    void Processor::reset()
    {
        gain = 1.0f;
    }

    void Processor::process (float* const* channels, int numChannels, int numSamples, bool playing, double startBeat,
                             double beatsPerSample, double stepBeats, float depth, float length)
    {
        auto beat = startBeat;
        stepBeats = std::max (1.0e-3, stepBeats);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto target = playing ? gainAt (std::fmod (std::max (0.0, beat), stepBeats) / stepBeats, depth, length) : 1.0f;
            gain += (target - gain) * smoothing;

            for (int c = 0; c < numChannels; ++c)
                channels[c][i] *= gain;

            beat += beatsPerSample;
        }
    }

    //==============================================================================
    const char* Plugin::xmlTypeName = "harmoniaPump";

    Plugin::Plugin (te::PluginCreationInfo info)
        : te::Plugin (info),
          position (te::createPosition (edit.tempoSequence))
    {
        auto* undoManager = getUndoManager();
        depthValue.referTo (state, "depth", undoManager, defaultDepth);
        rateValue.referTo (state, "rate", undoManager, (float) defaultRate);
        lengthValue.referTo (state, "length", undoManager, defaultLength);

        const auto percent = [] (float value)               { return juce::String (juce::roundToInt (value)) + "%"; };
        const auto number = [] (const juce::String& text)   { return text.getFloatValue(); };

        depth = addParam ("depth", "Depth", { 0.0f, 100.0f }, percent, number);
        rate = addParam ("rate", "Rate", { 0.0f, (float) getRates().size() - 1.0f, 1.0f },
                         [] (float value) { return pump::getName (getRates()[(size_t) juce::jlimit (0, (int) getRates().size() - 1,
                                                                                              juce::roundToInt (value))]); },
                         [] (const juce::String& text)
                         {
                             for (size_t i = 0; i < getRates().size(); ++i)
                                 if (text.trim() == pump::getName (getRates()[i]))
                                     return (float) i;

                             return (float) defaultRate;
                         });
        length = addParam ("length", "Length", { 10.0f, 100.0f }, percent, number);

        depth->attachToCurrentValue (depthValue);
        rate->attachToCurrentValue (rateValue);
        length->attachToCurrentValue (lengthValue);
    }

    Plugin::~Plugin()
    {
        notifyListenersOfDeletion();

        depth->detachFromCurrentValue();
        rate->detachFromCurrentValue();
        length->detachFromCurrentValue();
    }

    void Plugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        te::copyPropertiesToCachedValues (v, depthValue, rateValue, lengthValue);

        for (auto* parameter : getAutomatableParameters())
            parameter->updateFromAttachedValue();
    }

    Rate Plugin::getRate() const
    {
        return getRates()[(size_t) juce::jlimit (0, (int) getRates().size() - 1, juce::roundToInt (rate->getCurrentValue()))];
    }

    void Plugin::initialise (const te::PluginInitialisationInfo& info)
    {
        sampleRate = info.sampleRate;
        processor.prepare (info.sampleRate);
    }

    void Plugin::reset()
    {
        processor.reset();
    }

    void Plugin::applyToBuffer (const te::PluginRenderContext& context)
    {
        if (context.destBuffer == nullptr || context.bufferNumSamples <= 0)
            return;

        auto& buffer = *context.destBuffer;
        const auto numChannels = std::min (2, buffer.getNumChannels());
        float* channels[2] = {};

        for (int c = 0; c < numChannels; ++c)
            channels[c] = buffer.getWritePointer (c, context.bufferStartSample);

        // Where in the bar the block starts: the ducks fall on the bar's beats.
        position.set (context.editTime.getStart());
        const auto beatsPerBar = std::max (1, position.getTimeSignature().numerator);
        const auto beatInBar = std::fmod (std::max (0.0, position.getBeats().inBeats()), (double) beatsPerBar);
        const auto beatsPerSample = position.getTempo() / 60.0 / std::max (1.0, sampleRate);

        auto stepBeats = 1.0;

        switch (getRate())
        {
            case Rate::eighth:  stepBeats = 0.5; break;
            case Rate::quarter: stepBeats = 1.0; break;
            case Rate::half:    stepBeats = 2.0; break;
            case Rate::bar:     stepBeats = beatsPerBar; break;
        }

        processor.process (channels, numChannels, context.bufferNumSamples, context.isPlaying, beatInBar, beatsPerSample,
                           stepBeats, depth->getCurrentValue() / 100.0f, length->getCurrentValue() / 100.0f);
    }

    void registerType (te::Engine& engine)
    {
        engine.getPluginManager().createBuiltInType<Plugin>();
    }
}
