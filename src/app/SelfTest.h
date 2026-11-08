#pragma once

#include "core/Session.h"

namespace harmonia::app
{
    /** Command line: Harmonia.exe --selftest [--log=<file>] [--plugin=<path to .vst3>] [--no-device]

        Builds a project in code and checks the whole audio path: device, playback started
        immediately after building ("play when ready"), audible output, offline render,
        out-of-process plugin scan, instrument switching while playing, plugin window and
        note preview. Writes a report and returns the number of failed checks. */
    struct SelfTestOptions
    {
        juce::File logFile;
        juce::File renderDirectory;
        juce::String pluginPath;
        bool useDevice = true;

        /** Returns nullopt if the command line doesn't ask for a self-test. */
        static std::optional<SelfTestOptions> fromCommandLine (const juce::String&);
    };

    int runSelfTest (core::Session&, const SelfTestOptions&);
}
