#pragma once

#include "core/Session.h"

/** A track's delay: how much later, or earlier, than the rest of the song the track plays, in
    milliseconds. For a part that drags or rushes, or a sound with a slow attack that should land
    on the beat.

    Later: the track's sound is held back. Earlier: nothing can play before it is asked to, so the
    rest of the song waits for the track instead; the engine balances that the way it balances a
    slow plugin, and what is played live is heard that much later too.

    The delay sits at the fader end of the track's plugins, after its instrument and effects, so
    its sends are moved with it. It isn't one of the track's effects (audio::effects). */
namespace harmonia::audio::track_delay
{
    inline constexpr double maxMs = 100.0;      // either way

    /** How much later (above zero) or earlier (below) the track plays. */
    double getMs (te::AudioTrack&);

    /** In the current undo step (the caller begins the gesture). Kept within maxMs either way, to
        a tenth of a millisecond. */
    void setMs (te::AudioTrack&, double ms);

    /** "+12 ms", "-3.5 ms", "0 ms". */
    juce::String describe (double ms);

    /** A number typed by hand: "12", "-3,5", "+7 ms". Nothing if it isn't one. */
    std::optional<double> parse (const juce::String&);

    //==============================================================================
    /** What holds a track's delay and makes it: at most one per track, made by setMs. */
    class Plugin final : public te::Plugin
    {
    public:
        explicit Plugin (te::PluginCreationInfo);
        ~Plugin() override;

        static const char* getPluginName()      { return "Track delay"; }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return "Track delay"; }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "Delay"; }
        juce::String getSelectableDescription() override        { return "Track delay"; }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override    {}
        void reset() override;
        double getLatencySeconds() override;
        double getTailLength() const override;
        int getNumOutputChannelsGivenInputs (int numInputChannels) override     { return juce::jmin (numInputChannels, 2); }
        BusLayout getBusses() const override                                    { return BusLayout::singleStereoInOut(); }
        bool canBeAddedToClip() override                                        { return false; }
        bool canBeAddedToRack() override                                        { return false; }
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;

        juce::CachedValue<float> msValue;

    private:
        juce::AudioBuffer<float> line;          // the latest sound, as long as the longest delay
        int position = 0;                       // where the next sample goes
        std::atomic<double> sampleRate { 44100.0 };
        std::atomic<int> heldSamples { 0 };     // how far back the sound is read: the "later" part
        double reportedLatency = 0.0;           // the "earlier" part the engine knows of
        te::LambdaTimer restartTimer;

        void update();
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Plugin)
    };

    /** Lets the engine create it (and load songs that have one). Call once, right after the
        engine is made. */
    void registerType (te::Engine&);
}
