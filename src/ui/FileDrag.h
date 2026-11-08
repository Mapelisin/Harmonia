#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Files dragged inside the window (from the sound browser onto the arrangement): what the drag
    carries, so that a drop can treat them like files dragged in from the file explorer. */
namespace harmonia::ui::file_drag
{
    inline const juce::String& prefix()
    {
        static const juce::String text ("harmonia-files");
        return text;
    }

    /** The drag description for these files. */
    inline juce::var describe (const juce::StringArray& paths)
    {
        return prefix() + "\n" + paths.joinIntoString ("\n");
    }

    /** The files a drag carries (none if it isn't a drag of files). */
    inline juce::StringArray read (const juce::var& description)
    {
        if (! description.isString())
            return {};

        auto lines = juce::StringArray::fromLines (description.toString());

        if (lines.isEmpty() || lines[0] != prefix())
            return {};

        lines.remove (0);
        lines.removeEmptyStrings();
        return lines;
    }
}
