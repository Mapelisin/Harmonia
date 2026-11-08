#include "core/Tracks.h"

namespace harmonia::core::tracks
{
    namespace
    {
        const juce::Identifier kindId ("hTrackKind");
        constexpr const char* audioKind = "audio";
        constexpr const char* returnKind = "return";
    }

    Kind getKind (const te::AudioTrack& track)
    {
        const auto kind = track.state[kindId].toString();

        if (kind == audioKind)
            return Kind::audio;

        if (kind == returnKind)
            return Kind::fxReturn;

        return Kind::instrument;
    }

    void setKind (te::AudioTrack& track, Kind kind)
    {
        auto* undoManager = &track.edit.getUndoManager();

        if (kind == Kind::audio)
            track.state.setProperty (kindId, audioKind, undoManager);
        else if (kind == Kind::fxReturn)
            track.state.setProperty (kindId, returnKind, undoManager);
        else
            track.state.removeProperty (kindId, undoManager);
    }
}
