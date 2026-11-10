#pragma once

#include "audio/SampleData.h"
#include "core/Session.h"

/** Harmonia's sampler: an instrument that plays one sound file with notes. The root key plays
    the sound as it is, the keys above it higher and faster, those below lower and slower (as a
    tape would). Several notes at once, each fading in and out by Attack and Release; with One
    shot on a note plays the sound to its end whatever the key does.

    The file is read into memory when it is chosen (not on the audio thread) and stays where it
    is, as audio clips' files do. Which file and which root key are kept in the song, on the
    sampler's node, and in its presets. */
namespace harmonia::audio::sampler
{
    inline constexpr int defaultRoot = 60;              // middle C plays the sound as it is
    inline constexpr float bendSemitones = 2.0f;        // what the pitch wheel does at its ends

    /** How a note is played. */
    struct Settings
    {
        float gain = 1.0f;                  // linear
        float tune = 0.0f;                  // semitones
        double attackSeconds = 0.0, releaseSeconds = 0.1;
        bool oneShot = false;
        int root = defaultRoot;
    };

    /** The sound part, without Tracktion. */
    class Processor
    {
    public:
        Processor();
        ~Processor();

        void prepare (double sampleRate);

        /** Silence, at once. */
        void reset();

        /** The sound to play from now on (not on the audio thread; nullptr: none): taken at the
            next beginBlock, where the notes still playing the old one stop. */
        void setSample (SampleData::Ptr);

        /** Before the notes and the rendering of a block (audio thread): takes a new sound. */
        void beginBlock();

        /** `velocity` 0..1; `channel` 1..16 (its pitch wheel and pedal are the note's). */
        void noteOn (int note, float velocity, int channel, const Settings&);
        void noteOff (int note, int channel);

        /** The channel's pitch wheel, in semitones: the notes playing follow it. */
        void setPitchBend (int channel, float semitones);

        /** The channel's sustain pedal: while it is down, notes let go keep sounding. */
        void setSustain (int channel, bool down);

        /** Every note fades out by its release. */
        void allNotesOff();

        /** Adds the next `numSamples` to `left` and `right`. */
        void render (float* left, float* right, int numSamples);

        int getNumPlaying() const;

        static constexpr int maxVoices = 24;

    private:
        struct State;
        std::unique_ptr<State> state;
        double sampleRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE (Processor)
    };

    //==============================================================================
    /** The sampler as a track's instrument. */
    class Plugin final : public te::Plugin
    {
    public:
        explicit Plugin (te::PluginCreationInfo);
        ~Plugin() override;

        static const char* getPluginName()      { return "Sampler"; }
        static const char* xmlTypeName;

        juce::String getName() const override                   { return "Sampler"; }
        juce::String getPluginType() override                   { return xmlTypeName; }
        juce::String getShortName (int) override                { return "Sampler"; }
        juce::String getSelectableDescription() override        { return "Sampler"; }

        void initialise (const te::PluginInitialisationInfo&) override;
        void deinitialise() override    {}
        void reset() override;
        void midiPanic() override;
        int getNumOutputChannelsGivenInputs (int) override      { return 2; }
        BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
        bool takesMidiInput() override                          { return true; }
        bool isSynth() override                                 { return true; }
        bool producesAudioWhenNoAudioInput() override           { return true; }
        double getTailLength() const override                   { return 5.0; }
        void applyToBuffer (const te::PluginRenderContext&) override;
        void restorePluginStateFromValueTree (const juce::ValueTree&) override;

        juce::CachedValue<float> volumeValue, tuneValue, attackValue, releaseValue, modeValue;
        te::AutomatableParameter::Ptr volume, tune, attack, release, mode;

        /** Reads the sound file the song has for it again, right now (it happens by itself a
            moment after the file changes). */
        void updateSampleNow();

        /** The sound in memory (nullptr: none, or the file can't be read): for drawing it. */
        SampleData::Ptr getSample() const;

    private:
        class Loader;

        Processor processor;
        std::unique_ptr<Loader> loader;
        std::atomic<int> rootNow { defaultRoot };       // the root key, for the audio thread

        Settings getSettings() const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Plugin)
    };

    //==============================================================================
    // The sampler's sound, as the song keeps it. `sampler` is a sampler::Plugin.

    /** The sound file (none: an empty File). */
    juce::File getFile (const te::Plugin& sampler);

    /** A file is set but isn't there (moved or deleted): the sampler is silent. */
    bool isFileMissing (const te::Plugin& sampler);

    /** Plays this file from now on: one undo step. Returns an empty string, or the reason it
        can't (not a sound file). */
    juce::String setFile (te::Plugin& sampler, const juce::File&);

    /** The key that plays the sound as it is (into the current undo step). */
    int getRoot (const te::Plugin& sampler);
    void setRoot (te::Plugin& sampler, int note);

    /** Lets the engine create the sampler (and load projects that have one). Call once, right
        after the engine is made. */
    void registerType (te::Engine&);
}
