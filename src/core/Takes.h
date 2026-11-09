#pragma once

#include "core/Tracktion.h"

/** Takes of an audio clip: the passes of a recording made in a loop, kept in the one clip that
    plays one of them. Each take is a sound file as long as the loop; the clip's source is the
    take chosen. A clip split in two keeps its takes in both pieces, so each piece can play its
    own: that's how the best parts of several takes are put together.

    Kept in the clip's own node (not Tracktion's takes, which need its projects and render comps
    in the background). Choosing and dropping go into the current undo step: the caller starts it. */
namespace harmonia::core::takes
{
    /** How many takes the clip has (0 for a clip recorded in one pass, or not recorded). */
    int getCount (const te::Clip&);

    /** The take's sound file. */
    juce::File getFile (const te::Clip&, int index);

    /** "Take 2". */
    juce::String getName (int index);

    /** The take the clip plays, or -1 (none, or its source is another file). */
    int getCurrent (const te::Clip&);

    /** The clip plays this take from now on. False if there's no such take or its file is gone. */
    bool choose (te::AudioClipBase&, int index);

    /** Forgets the takes the clip doesn't play (their files stay on the disk). */
    void keepOnlyCurrent (te::AudioClipBase&);

    /** After a recording in a loop: the takes the engine made become the clip's own, and the
        clip plays the last one recorded in full. Into the current undo step. Returns how many
        clips got takes. */
    int adoptRecorded (te::Edit&);
}
