#include "core/Tracks.h"

namespace harmonia::core::tracks
{
    namespace
    {
        const juce::Identifier kindId ("hTrackKind");
        constexpr const char* audioKind = "audio";
    }

    Kind getKind (const te::AudioTrack& track)
    {
        return track.state[kindId].toString() == audioKind ? Kind::audio : Kind::instrument;
    }

    void setKind (te::AudioTrack& track, Kind kind)
    {
        auto* undoManager = &track.edit.getUndoManager();

        if (kind == Kind::audio)
            track.state.setProperty (kindId, audioKind, undoManager);
        else
            track.state.removeProperty (kindId, undoManager);
    }
}
