#pragma once

#include <juce_core/juce_core.h>

namespace harmonia::core::project_files
{
    /** What a project file's name ends with (Windows opens these with Harmonia). */
    inline constexpr const char* extension = ".harmonia";

    /** The project a command line names: Explorer passes the double-clicked file, quoted (and
        some launchers don't quote a path with spaces). Only an existing project file given by its
        full path counts; anything else gives no file. */
    juce::File fromCommandLine (const juce::String& commandLine);
}
