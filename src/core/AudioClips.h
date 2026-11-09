#pragma once

#include "core/Tracktion.h"

/** How Harmonia has the engine play an audio clip. */
namespace harmonia::core::audio_clips
{
    /** Sets an audio clip up the way Harmonia plays it, for a clip just added, one whose tempo or
        pitch changed (core::audio_tempo) and those of an opened project:
        - at its file's own pitch: the engine moves a loop that names its key to the song's by itself;
        - following the tempo or re-pitched, stretched while it plays, by the stretcher Harmonia has;
          played as it is, the engine's way;
        - a compressed file (FLAC, Ogg, MP3) has the WAV copy the engine plays it from made here,
          right away; stretched, it is read straight from the disk, which only works for WAV and
          AIFF: the clip is given a WAV of it instead, in the folder the song's new sound files go
          to (with the recordings).
        The engine would otherwise make its copies of a file (stretched, or decoded) in the
        background, on a timer, and an export waits for them forever: it holds up that timer.
        Settings go into the current undo step. */
    void prepareForPlayback (te::AudioClipBase&);

    //==============================================================================
    // Looping: the part of the file the clip plays repeats for as long as the clip is; made
    // longer, the clip plays it again. Undoable (the caller starts the gesture).

    bool isLooping (const te::AudioClipBase&);

    /** On: what the clip plays now becomes the loop. Off: it plays once, from where the loop
        started (and is no longer than the file allows). */
    void setLooping (te::AudioClipBase&, bool);

    /** How long one time round the loop is in the song, in beats where the clip starts (0 if
        the clip doesn't loop). */
    double getLoopLengthBeats (const te::AudioClipBase&);

    /** The song beat where the loop's first time round starts: the clip's start, or before it if
        the clip's start was trimmed (or it is the later half of a split). */
    double getLoopSongStartBeat (const te::AudioClipBase&);

    /** The seconds of the file that repeat (empty if the clip doesn't loop). */
    juce::Range<double> getLoopFileSeconds (const te::AudioClipBase&);

    //==============================================================================
    // Crossfades: where two audio clips of a track overlap, the earlier fades out and the later
    // fades in over the overlap, instead of both playing at full. On for every clip unless
    // turned off; a clip's own fades apply where nothing overlaps it.

    bool hasCrossfades (const te::AudioClipBase&);
    void setCrossfades (te::AudioClipBase&, bool);
}
