#pragma once

#include "core/Tracktion.h"

namespace harmonia::core::clip_links
{
    /*  Linked copies of MIDI clips (decision 18): clips in one link group share their notes,
        so editing any of them changes all of them. Positions and lengths stay independent.

        Implemented with Harmonia's own `hLinkGroup` clip attribute rather than Tracktion's
        linkID: Tracktion re-clones linked clips asynchronously without undo, which would
        leave undo history pointing at nodes that no longer exist. Here the copies are
        updated in the same undo transaction as the edit, so undo/redo restore every copy
        exactly.

        Every change is recorded in the Edit's UndoManager. */

    bool isLinked (const te::MidiClip&);

    /** All MIDI clips sharing `clip`'s link group, including `clip` itself (just `clip` if unlinked). */
    juce::Array<te::MidiClip*> getGroup (te::MidiClip& clip);

    /** Duplicates `clip` on its track at `startBeat`. If `linked`, the copy joins (or starts)
        the source's link group; otherwise the copy is independent. Returns nullptr on failure. */
    te::MidiClip* duplicate (te::MidiClip& clip, double startBeat, bool linked);

    /** Makes `clip` independent ("make unique"). A group left with a single clip is dissolved. */
    void unlink (te::MidiClip& clip);

    /** Copies `source`'s notes to every other clip in its link group. Only differences are
        written, so repeated calls during a mouse drag stay cheap in the undo history. */
    void syncFrom (te::MidiClip& source);
}
