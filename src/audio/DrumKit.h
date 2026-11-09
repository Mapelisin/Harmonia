#pragma once

#include "core/Session.h"

/** Harmonia's drum machine: an instrument for the General MIDI drum keys (kick on C1, snare on
    D1, hats on F#1 and A#1...) that makes its drum sounds itself, out of falling tones, noise and
    metallic clangs. A drum part sounds like drums with no plugin and no samples. */
namespace harmonia::audio::drum_kit
{
    /** How the kit sounds. */
    enum class Kit
    {
        punchy,     // tight and bright, like the classic drum machines
        boomy,      // "808": a long deep kick, a snappy snare, thin hats
        soft        // "Lo-fi": darker and rounder, the top rolled off
    };

    const std::vector<Kit>& getKits();
    juce::String getName (Kit);         // "Punchy", "808", "Lo-fi"

    /** What a key plays. */
    enum class Sound
    {
        none,
        kick, snare, rim, clap,
        closedHat, pedalHat, openHat,
        tom, crash, ride, rideBell,
        cowbell, tambourine, shaker, woodblock, hand
    };

    /** The sound of a General MIDI drum key (35 to 81); none for the others. */
    Sound getSound (int note);

    inline constexpr Kit defaultKit = Kit::punchy;
    inline constexpr float defaultTune = 0.0f;          // semitones
    inline constexpr float defaultDecay = 100.0f;       // percent of how long each drum rings

    /** The sound part, without Tracktion. A hit rings out by itself: letting the key go doesn't
        stop it, but a closed hat stops an open one, and hitting a drum again stops it ringing. */
    class Processor
    {
    public:
        Processor();
        ~Processor();

        void prepare (double sampleRate);

        /** Silence, at once. */
        void reset();

        /** A hit. `velocity` 0..1; `tune` in semitones; `decay` scales how long it rings (1: as made). */
        void noteOn (int note, float velocity, Kit, float tune, float decay);

        /** Everything ringing fades out within a few milliseconds. */
        void stopAll();

        /** Adds the next `numSamples` of the kit to `left` and `right`. */
        void render (float* left, float* right, int numSamples);

        /** How many hits are still ringing. */
        int getNumPlaying() const;

        static constexpr int maxVoices = 32;

    private:
        struct Voices;
        std::unique_ptr<Voices> voices;
        double sampleRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE (Processor)
    };

    //==============================================================================
    /** The drum machine as a track's instrument. */
    class Plugin final : public te::Plugin
    {
    public:
        explicit Plugin (te::PluginCreationInfo);
        ~Plugin() override;

        static const char* getPluginName()      { return "Drums"; }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return "Drums"; }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "Drums"; }
        juce::String getSelectableDescription() override        { return "Drums"; }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override    {}
        void reset() override;
        void midiPanic() override;
        int getNumOutputChannelsGivenInputs (int) override      { return 2; }
        BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
        bool takesMidiInput() override                          { return true; }
        bool isSynth() override                                 { return true; }
        bool producesAudioWhenNoAudioInput() override           { return true; }
        double getTailLength() const override                   { return 3.0; }
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;

        juce::CachedValue<float> kitValue, tuneValue, decayValue;
        te::AutomatableParameter::Ptr kit, tune, decay;

        /** The kit set now. */
        Kit getKit() const;

    private:
        Processor processor;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Plugin)
    };

    /** Lets the engine create the drum machine (and load projects that have one). Call once,
        right after the engine is made. */
    void registerType (te::Engine&);
}
