#pragma once

#include "core/Tracktion.h"

/** Plays MIDI with a simple built-in sound (soft tones; drums on channel 10), so that a MIDI
    file can be heard in the sound browser before it goes into the song, where it gets a real
    instrument. */
namespace harmonia::audio::midi_render
{
    inline constexpr double defaultSampleRate = 44100.0;

    /** Only the start of a long file: a preview is for recognising it. */
    inline constexpr double maxSeconds = 60.0;

    /** The file's notes as stereo sound, from the file's start until its last note has rung out
        (at most `limitSeconds`: notes still sounding then fade out). Empty without notes. */
    juce::AudioBuffer<float> render (const juce::MidiFile&, double sampleRate = defaultSampleRate,
                                     double limitSeconds = maxSeconds);

    /** The same for a .mid file; empty if it can't be read. */
    juce::AudioBuffer<float> render (const juce::File&, double sampleRate = defaultSampleRate,
                                     double limitSeconds = maxSeconds);
}
