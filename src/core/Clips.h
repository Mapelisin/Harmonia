#pragma once

#include "core/Project.h"

namespace harmonia::core::clips
{
    /** Beats per bar of the song (the first time signature). */
    double getBeatsPerBar (const te::Edit&);

    /** Song beat where the clip's content beat 0 is (clip start minus its offset). */
    double getContentStartBeat (const te::Clip&);

    double getStartBeat (const te::Clip&);
    double getEndBeat (const te::Clip&);

    /** The track's first MIDI clip (the one the piano roll edits until the arrangement exists). */
    te::MidiClip* getMainClip (te::AudioTrack&);

    /** The track's first MIDI clip; creates an empty one at the start of the song if there is none. */
    te::MidiClip* getOrAddMainClip (Project&, te::AudioTrack&, double lengthBeats);

    /** Lengthens `clip` and its linked copies so that content up to `contentEndBeat` plays.
        The new end is rounded up to a whole bar. Clips are never shortened. Undoable. */
    void extendToCover (te::MidiClip& clip, double contentEndBeat);
}
