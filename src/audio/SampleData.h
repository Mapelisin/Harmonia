#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** A sound file in memory, for the instruments that play samples (the drum machine's keys,
        the sampler). Never changed once loaded: the audio thread reads it while it plays. */
    struct SampleData final : juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

        juce::AudioBuffer<float> audio;     // one or two channels
        double sampleRate = 44100.0;
        bool cut = false;                   // the file is longer than maxSeconds: this is its beginning

        /** At most this much of a file is loaded: a hit or a note, not a song. */
        static constexpr double maxSeconds = 30.0;

        double getLengthSeconds() const noexcept    { return sampleRate > 0.0 ? audio.getNumSamples() / sampleRate : 0.0; }

        /** Reads the file (not on the audio thread). nullptr and the reason if it can't be read. */
        static Ptr load (te::Engine&, const juce::File&, juce::String& error);

        /** Whether the file can be played, without reading it all: an empty string, or why not. */
        static juce::String check (te::Engine&, const juce::File&);

        /** The sound at a place between its samples (`position` in samples of `channel`; outside
            the sound: silence). Four-point interpolation: a sound played at its own speed comes
            out as it is. */
        float getSampleAt (int channel, double position) const noexcept;
    };
}
