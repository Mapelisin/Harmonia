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

    //==============================================================================
    // The arrangement. Positions are song beats; every change is undoable.

    /** The track's MIDI clips in time order. */
    juce::Array<te::MidiClip*> getMidiClips (te::AudioTrack&);

    /** False once the clip has been deleted (also by undo) or its track has. */
    bool isInEdit (const te::Clip&);

    /** Where the last clip of the song ends (0 for an empty song). */
    double getSongEndBeat (const te::Edit&);

    /** Puts the clip at `startBeat` (at least 0) on `track`, with its length and notes. */
    void move (te::MidiClip&, double startBeat, te::AudioTrack& track);

    /** Moves the clip's start, its notes staying where they are in the song: hides or shows
        the beginning. Not before the content start, and at least `minLengthBeats` long. */
    void setStart (te::MidiClip&, double startBeat, double minLengthBeats);

    /** Moves the clip's end, its notes staying where they are. At least `minLengthBeats` long. */
    void setEnd (te::MidiClip&, double endBeat, double minLengthBeats);

    /** Deletes clips. A link group left with one clip stops being a group. */
    void remove (const juce::Array<te::MidiClip*>&);
}
