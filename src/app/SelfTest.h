#pragma once

#include "core/Session.h"

namespace harmonia::app
{
    /** Command line: Harmonia.exe --selftest [--log=<file>] [--plugin=<path to .vst3>] [--no-device] [--quiet]

        Builds a project in code and checks the whole audio path: device, playback started
        immediately after building ("play when ready"), audible output, the timing of played
        notes (latency compensation and jitter), offline render, out-of-process plugin scan,
        instrument switching while playing, plugin window and note preview. Writes a report and
        returns the number of failed checks.

        --quiet mutes the master output: everything runs and is measured on the track, but
        nothing comes out of the speakers. */
    struct SelfTestOptions
    {
        juce::File logFile;
        juce::File renderDirectory;
        juce::String pluginPath;
        bool useDevice = true;
        bool quiet = false;

        /** Returns nullopt if the command line doesn't ask for a self-test. */
        static std::optional<SelfTestOptions> fromCommandLine (const juce::String&);
    };

    int runSelfTest (core::Session&, const SelfTestOptions&);
}
