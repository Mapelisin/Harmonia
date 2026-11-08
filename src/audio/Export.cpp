#include "audio/Export.h"

namespace harmonia::audio
{
    juce::String exportWav (te::Edit& edit, const juce::File& file)
    {
        const auto songEnd = edit.getLength();

        if (songEnd <= te::TimeDuration())
            return "Nothing to export: the song has no clips";

        if (! file.getParentDirectory().createDirectory())
            return "Cannot create folder: " + file.getParentDirectory().getFullPathName();

        if (file.existsAsFile() && ! file.deleteFile())
            return "Cannot overwrite " + file.getFullPathName() + " (is it open in another program?)";

        const te::TimeRange range (te::TimePosition(), te::TimePosition() + songEnd + te::TimeDuration::fromSeconds (exportTailSeconds));
        const auto tracks = te::toBitSet (te::getAllTracks (edit));

        // useThread = false: renders right here, without Tracktion's progress window.
        if (! te::Renderer::renderToFile ("Export", file, edit, range, tracks, true, false, {}, false))
            return "Export failed: " + file.getFullPathName();

        return {};
    }
}
