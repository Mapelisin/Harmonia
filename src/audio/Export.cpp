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

        // What you hear is what you get: Tracktion unmutes the tracks it renders, so muted ones
        // (and, while some are soloed, the others) are left out here.
        const auto anySolo = edit.areAnyTracksSolo();
        juce::Array<te::Track*> audible;
        auto anyAudioTrack = false;

        for (auto* track : te::getAllTracks (edit))
        {
            if (dynamic_cast<te::AudioTrack*> (track) == nullptr)
            {
                audible.add (track);
                continue;
            }

            if (track->isMuted (true) || (anySolo && ! (track->isSolo (true) || track->isSoloIsolate (true))))
                continue;

            audible.add (track);
            anyAudioTrack = true;
        }

        if (! anyAudioTrack)
            return "Nothing to export: every track is muted";

        const te::TimeRange range (te::TimePosition(), te::TimePosition() + songEnd + te::TimeDuration::fromSeconds (exportTailSeconds));
        const auto tracks = te::toBitSet (audible);

        // useThread = false: renders right here, without Tracktion's progress window.
        if (! te::Renderer::renderToFile ("Export", file, edit, range, tracks, true, false, {}, false))
            return "Export failed: " + file.getFullPathName();

        return {};
    }
}
