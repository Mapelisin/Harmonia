#pragma once

#include "core/Project.h"

/** Freezing a track: its sound through its instrument and effects is written to a file, and the
    track plays that file instead, its instrument and effects turned off: a track with heavy
    plugins then costs next to nothing. Unfrozen, it is as it was.

    The file has the track's sound before its fader: volume, pan, the sends and the group it
    plays into go on working on a frozen track. While it's frozen its clips, instrument and
    effects aren't heard, so changes to them wait until it is unfrozen.
    Freezing and unfreezing are one undo step each. */
namespace harmonia::audio::freeze
{
    bool isFrozen (const te::AudioTrack&);

    /** A song track with clips that isn't frozen yet. */
    bool canFreeze (te::AudioTrack&);

    /** Renders and freezes the track. Blocks until done. Returns an error message, empty on success. */
    juce::String freeze (core::Project&, te::AudioTrack&);

    /** The track plays its clips through its instrument and effects again. */
    void unfreeze (core::Project&, te::AudioTrack&);

    /** The file a frozen track plays (for tests). */
    juce::File getFile (const te::AudioTrack&);
}
