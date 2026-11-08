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

    std::optional<juce::Colour> getColour (const te::Track& track)
    {
        const auto colour = track.getColour();

        if (colour.isTransparent())
            return std::nullopt;

        return colour;
    }

    void setColour (te::Track& track, std::optional<juce::Colour> colour)
    {
        if (colour.has_value() && ! colour->isTransparent())
            track.setColour (*colour);
        else
            track.state.removeProperty (te::IDs::colour, &track.edit.getUndoManager());
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
