#pragma once

#include "core/Session.h"

/** Harmonia's limiter: raises the sound and keeps its peaks under a ceiling, so a song can be made
    louder without clipping. It looks 2 ms ahead, so each peak is turned down smoothly before it
    passes instead of being cut off. */
namespace harmonia::audio::limiter
{
    inline constexpr double lookaheadSeconds = 0.002;
    inline constexpr float maxGainDb = 24.0f, defaultCeilingDb = -1.0f, defaultReleaseMs = 150.0f;

    /** The sound part, without Tracktion. */
    class Processor
    {
    public:
        void prepare (double sampleRate, int maxChannels);
        void reset();

        /** How long the sound is held back to look ahead. */
        int getLatencySamples() const noexcept      { return length - 1; }

        /** In place: up by `gainDb`, then held under `ceilingDb`. `releaseMs`: how fast it lets
            the level come back up after a peak. */
        void process (float* const* channels, int numChannels, int numSamples, float gainDb, float ceilingDb, float releaseMs);

        /** How far down the latest sample was turned (0 or less), for a meter. */
        float getReductionDb() const noexcept       { return reductionDb; }

    private:
        double sampleRate = 44100.0;
        int length = 1;                     // the look-ahead window, in samples
        int position = 0;                   // in the rings below
        juce::AudioBuffer<float> delayed;   // the sound, held back by length - 1 samples
        std::vector<float> wanted;          // the gain each of the latest samples needs
        std::vector<double> smoothing;      // the released gain of the latest samples, averaged
        double smoothingSum = 0.0;
        double released = 1.0;
        float reductionDb = 0.0f;
    };

    //==============================================================================
    /** The limiter as an effect for a track's or the master's chain. */
    class Plugin final : public te::Plugin
    {
    public:
        explicit Plugin (te::PluginCreationInfo);
        ~Plugin() override;

        static const char* getPluginName()      { return "Limiter"; }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return "Limiter"; }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "Limiter"; }
        juce::String getSelectableDescription() override        { return "Limiter"; }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override    {}
        void reset() override;
        double getLatencySeconds() override;
        double getTailLength() const override;
        int getNumOutputChannelsGivenInputs (int numInputChannels) override     { return juce::jmin (numInputChannels, 2); }
        BusLayout getBusses() const override                                    { return BusLayout::singleStereoInOut(); }
        void applyToBuffer (const te::PluginRenderContext&) override;

        juce::CachedValue<float> gainValue, ceilingValue, releaseValue;
        te::AutomatableParameter::Ptr gain, ceiling, release;

    private:
        Processor processor;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Plugin)
    };

    /** Lets the engine create the limiter (and load projects that have one). Call once, right
        after the engine is made. */
    void registerType (te::Engine&);
}
