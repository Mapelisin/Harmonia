#pragma once

#include "Common.h"

namespace harmonia::spike
{
    struct SelfTestOptions
    {
        juce::File logFile;
        juce::File renderDirectory;
        juce::String pluginPath;   // optional VST3 instrument to test
        bool testLiveDevice = true;
    };

    /** Runs the automated checks and writes a report to options.logFile.
        Returns 0 when every check passed. */
    int runSelfTest (te::Engine&, const SelfTestOptions&);
}
