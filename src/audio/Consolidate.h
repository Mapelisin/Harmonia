#pragma once

#include "core/Project.h"

/** One clip of what a track has in a stretch of time (Ctrl+J): its clips there, and the parts of
    clips that reach into it, become a single clip from one edge of the stretch to the other.

    Notes: one clip with all the notes and controllers as they play (a clip that repeats is
    written out).

    Sound: the clips are rendered as they sound together (each with its gain, fades, stretching
    and pitch, crossfaded where they overlap) to a new file among the song's recordings. The
    track's effects, volume and pan aren't in the file: they go on being applied to the new clip.
    If any of the clips followed the tempo, so does the new one, its beats being where the song's
    were: it stays on the grid at any tempo the song is given later. */
namespace harmonia::audio::consolidate
{
    /** What is needed to make one clip of a stretch of a track. Made before anything in the song
        changes: rendering takes a moment, and can fail. */
    struct Piece
    {
        te::AudioTrack* track = nullptr;
        double fromBeat = 0.0, toBeat = 0.0;
        juce::File sound;               // of an audio track: what its clips play there, rendered
        bool followsTempo = false;      // of an audio track: one of those clips followed the tempo
    };

    /** How many clips of the track are in the stretch, or reach into it. */
    int countClips (te::AudioTrack&, double fromBeat, double toBeat);

    /** Nothing if the track has no clips there, or its sound couldn't be rendered (`error` then
        says why). Blocks while an audio track is rendered. Changes nothing in the song. */
    std::optional<Piece> prepare (te::AudioTrack&, double fromBeat, double toBeat, juce::String& error);

    /** Puts the one clip in place of what the track has in the stretch. Into the current undo
        step. Returns it (nullptr if it couldn't be made: nothing is taken out then). */
    te::Clip* apply (core::Project&, const Piece&);
}
