#pragma once

#include "core/Session.h"

/** Harmonia's pump: the sound ducks on every beat (or eighth, half bar, bar) and swells back,
    in time with the song, as if a kick drum were pushing it down (the "sidechain" pumping of
    dance music). No kick needed: it follows the song's tempo. */
namespace harmonia::audio::pump
{
    /** How often it ducks. */
    enum class Rate
    {
        eighth,
        quarter,    // every beat
        half,
        bar
    };

    const std::vector<Rate>& getRates();
    juce::String getName (Rate);        // "1/8", "1/4", "1/2", "1 bar"

    inline constexpr float defaultDepth = 70.0f, defaultLength = 60.0f;     // percent
    inline constexpr Rate defaultRate = Rate::quarter;

    /** The level (gain, 0..1) at `phase` (0..1) of a step: down by `depth` (0..1) right at its
        start, held there a moment and swelling back up, all the way by `length` (0..1) of it. */
    float gainAt (double phase, float depth, float length);

    /** The sound part, without Tracktion. */
    class Processor
    {
    public:
        void prepare (double sampleRate);
        void reset();

        /** In place. `startBeat`: the song beat of the first sample; `beatsPerSample`: how far a
            sample goes; `stepBeats`: how many beats from one duck to the next. With `playing`
            false nothing ducks (the song isn't going, so there is no beat to follow). */
        void process (float* const* channels, int numChannels, int numSamples, bool playing, double startBeat,
                      double beatsPerSample, double stepBeats, float depth, float length);

        /** The gain the latest sample got, for a meter. */
        float getGain() const noexcept      { return gain; }

    private:
        double sampleRate = 44100.0;
        float gain = 1.0f, smoothing = 1.0f;
    };

    //==============================================================================
    /** The pump as an effect for a track's (or the master's) chain. */
    class Plugin final : public te::Plugin
    {
    public:
        explicit Plugin (te::PluginCreationInfo);
        ~Plugin() override;

        static const char* getPluginName()      { return "Pump"; }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return "Pump"; }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "Pump"; }
        juce::String getSelectableDescription() override        { return "Pump"; }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override    {}
        void reset() override;
        int getNumOutputChannelsGivenInputs (int numInputChannels) override     { return juce::jmin (numInputChannels, 2); }
        BusLayout getBusses() const override                                    { return BusLayout::singleStereoInOut(); }
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;

        juce::CachedValue<float> depthValue, rateValue, lengthValue;
        te::AutomatableParameter::Ptr depth, rate, length;

        /** The rate set now. */
        Rate getRate() const;

    private:
        Processor processor;
        te::tempo::Sequence::Position position;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Plugin)
    };

    /** Lets the engine create the pump (and load projects that have one). Call once, right after
        the engine is made. */
    void registerType (te::Engine&);
}
