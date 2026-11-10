#pragma once

#include "audio/SampleData.h"
#include "core/Session.h"

/** Samples in the drum machine: a key of the kit plays a sound file (your own kick, clap...)
    instead of the sound the kit makes itself.

    Which key plays which file is kept in the song, on the drum machine's node, with the sample's
    own volume and tuning; the files stay where they are (as audio clips' do). A key whose file
    has gone plays the kit's own sound again. Presets and copies of the drum machine keep them.
    The kit's Tune and Decay knobs work on samples too: Decay under 100% cuts them short. */
namespace harmonia::audio::drum_kit
{
    using audio::SampleData;

    /** What a key plays in place of the kit's own sound (nothing if `data` is null). */
    struct KeySample
    {
        SampleData::Ptr data;
        float gain = 1.0f;      // linear
        float tune = 0.0f;      // semitones
    };

    /** Every key's sample, as the sound part reads them: never changed once made. */
    struct SampleBank final : juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<SampleBank>;
        std::array<KeySample, 128> keys;
    };

    /** One hit of a sample, playing to its end. */
    struct SampleVoice
    {
        const SampleData* data = nullptr;
        int note = -1;
        bool active = false;
        juce::int64 age = 0;                // output samples since the hit

        /** `velocity` 0..1; `tune` in semitones (the kit's and the key's together); `decay` under
            1 cuts the sample short, fading out. */
        void start (int key, const KeySample&, float velocity, float tune, float decay, double rate);

        /** Fades out within a few milliseconds. */
        void stop (double rate);

        /** Adds the next `numSamples` to `left` and `right`. */
        void render (float* left, float* right, int numSamples);

    private:
        double position = 0.0, step = 1.0, endAt = 0.0, fadeFrom = 0.0;
        float level = 1.0f, fade = 1.0f, fadeFactor = 1.0f;
        bool fading = false;
    };

    //==============================================================================
    // What the keys play, as the song keeps it. `drums` is the drum machine (drum_kit::Plugin).
    // Changes go through undo; the sound follows a moment later (Plugin::updateSamplesNow: at once).

    struct SampleInfo
    {
        juce::File file;
        float gainDb = 0.0f;
        float tune = 0.0f;
        bool missing = false;       // the file isn't there: the key plays the kit's own sound
    };

    inline constexpr float minSampleGainDb = -24.0f, maxSampleGainDb = 12.0f, maxSampleTune = 24.0f;

    /** The key's sample, if it has one. */
    std::optional<SampleInfo> getSample (const te::Plugin& drums, int note);

    /** The keys that have samples, lowest first. */
    std::vector<int> getSampledKeys (const te::Plugin& drums);

    /** The key plays this file from now on: one undo step. Returns an empty string, or the reason
        it can't (not a sound file, not a key). */
    juce::String setSample (te::Plugin& drums, int note, const juce::File&);

    /** Several keys at once play these files from now on (a loop cut into its hits): into the
        current undo step, which the caller starts. Keys that aren't the kit's and files that
        aren't sounds are left out. */
    void setSamples (te::Plugin& drums, const std::vector<std::pair<int, juce::File>>& files);

    /** The key plays the kit's own sound again: one undo step. */
    void clearSample (te::Plugin& drums, int note);

    /** The sample's own volume and tuning: into the current undo step (the caller starts it). */
    void setSampleGainDb (te::Plugin& drums, int note, float db);
    void setSampleTune (te::Plugin& drums, int note, float semitones);

    /** What the key plays, for its label: the sample's name, else the kit's drum ("Kick"), else
        empty. */
    juce::String getKeyName (const te::Plugin& drums, int note);

    /** The node (a child of the drum machine's) that keeps the samples: presets keep it too. */
    const juce::Identifier& getSamplesTag();
}
