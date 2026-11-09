#include "core/ProjectFiles.h"

namespace harmonia::core::project_files
{
    juce::File fromCommandLine (const juce::String& commandLine)
    {
        juce::StringArray candidates;
        candidates.addTokens (commandLine, true);

        // A path with spaces that came without quotes is the whole line.
        candidates.add (commandLine);

        for (const auto& candidate : candidates)
        {
            const auto path = candidate.unquoted().trim();

            // Checked before making a File of it: a relative path isn't one.
            if (! juce::File::isAbsolutePath (path))
                continue;

            if (const juce::File file (path); file.existsAsFile() && file.hasFileExtension (extension))
                return file;
        }

        return {};
    }
}
