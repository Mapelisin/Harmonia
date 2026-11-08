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
}
