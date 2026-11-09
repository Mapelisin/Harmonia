#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Two small plugins for testing plugins in their own processes: the test program's plugin
    processes can load them (format "HarmoniaTest"), and each can be told to crash or hang. */
namespace harmonia::test::plugins
{
    inline constexpr const char* formatName = "HarmoniaTest";

    std::unique_ptr<juce::AudioPluginFormat> createFormat();

    /** An instrument: while a note is held every sample is Level (both channels); it reports
        the last note it got in "Last note" (note / 127), passes its MIDI on, has a latency of 64
        samples and the programs One, Two and Three. */
    juce::PluginDescription synth();

    /** An effect: its input times twice Gain (so 0.5 leaves it as it is); mono or stereo. */
    juce::PluginDescription gain();

    /** The parameters, in order (the gain has the first three, its first being Gain). */
    enum Parameter { level, crash, hang, lastNote };
}
