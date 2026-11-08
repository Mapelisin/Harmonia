#pragma once

#include "core/Tracktion.h"

/** Editing the song's time on every track at once: bars inserted, deleted or repeated, with the
    clips, automation, tempo and markers after them moving along. Clips across an edge are split
    there first. Beats are song beats; every change goes into the current undo step (the caller
    begins it). */
namespace harmonia::core::sections
{
    /** Empty space of `lengthBeats` at `startBeat`: everything from there on moves later. */
    void insertSpace (te::Edit&, double startBeat, double lengthBeats);

    /** Deletes what every track plays from `startBeat` to `endBeat` (and the markers there);
        everything after moves earlier to close the gap. */
    void deleteRange (te::Edit&, double startBeat, double endBeat);

    /** Plays the stretch twice: a copy of it goes right after it (MIDI clips linked to their
        originals, as Ctrl+B makes them; markers copied too) and what came after moves later.
        Returns the copies. */
    juce::Array<te::Clip*> duplicateRange (te::Edit&, double startBeat, double endBeat);
}
