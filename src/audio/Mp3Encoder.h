#pragma once

#include <juce_core/juce_core.h>

/** MP3 files, made by the system's own encoder (Windows: Media Foundation's): Harmonia carries
    no encoder of its own. */
namespace harmonia::audio::mp3
{
    /** The system can make MP3 files at this bit rate (stereo). */
    bool isAvailable (int kbps);

    /** The sample rates an MP3 file can have (the export renders at one of them). */
    bool canUseSampleRate (double);

    /** Encodes a WAV (mono or stereo, at a rate canUseSampleRate takes) into an MP3 at `kbps`
        (128, 192, 256 or 320; constant bit rate). Blocks until done. An error, or empty. */
    juce::String encode (const juce::File& wav, const juce::File& mp3, int kbps);
}
