#pragma once

#include "core/Project.h"

/** Sends and return tracks: effects shared by several tracks (one reverb for the whole song).

    A return track (core::tracks::Kind::fxReturn) has no clips. Its list starts with Tracktion's aux
    return on a bus of its own, then its effects, then the fader. A track sends to it with an aux send
    on that bus, after the track's fader (post-fader: the send follows the track's volume, and a muted
    track sends nothing). Return tracks sit after the song's tracks, only show in the mixer and are
    solo-safe: soloing a track keeps its reverb. */
namespace harmonia::audio::sends
{
    /** What a new return track starts with. */
    enum class Preset { empty, reverb, delay };

    /** Most return tracks a project can have. */
    constexpr int maxReturnTracks = 16;

    /** Adds a return track after the others, named after the preset, with that effect fully wet;
        one undo step. nullptr with the reason in `error` if there are already maxReturnTracks. */
    te::AudioTrack* addReturnTrack (core::Project&, Preset, juce::String& error);

    /** Deletes a return track and every send to it, as one undo step. */
    void removeReturnTrack (core::Project&, te::AudioTrack& returnTrack);

    /** Renames a return track (one undo step); its send controls show the new name. */
    void renameReturnTrack (core::Project&, te::AudioTrack& returnTrack, const juce::String& name);

    /** The bus a return track listens to, or -1 if it isn't a return track. */
    int getBus (const te::AudioTrack& returnTrack);

    /** The track's send to a return track, or nullptr if it has none. */
    te::AuxSendPlugin* getSend (const te::AudioTrack& track, const te::AudioTrack& returnTrack);

    /** How much the track sends, as a fader position (0..1, like the volume faders; 0 = nothing).
        0 without a send. */
    float getLevel (const te::AudioTrack& track, const te::AudioTrack& returnTrack);

    /** The same in decibels (-100 for nothing). */
    float getLevelDb (const te::AudioTrack& track, const te::AudioTrack& returnTrack);

    /** Sets how much the track sends (a fader position), adding the send if the track has none.
        Part of the current undo transaction: the caller starts it (one drag = one step).
        Returns false if no send could be added (the track is full, or isn't a song track). */
    bool setLevel (te::AudioTrack& track, te::AudioTrack& returnTrack, float faderPosition);
    bool setLevelDb (te::AudioTrack& track, te::AudioTrack& returnTrack, float db);
}
