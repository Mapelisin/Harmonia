#include "core/SongNodes.h"

namespace harmonia::core::song_nodes
{
    juce::ValueTree getOrCreate (te::Edit& edit, const juce::Identifier& type)
    {
        if (auto existing = edit.state.getChildWithName (type); existing.isValid())
            return existing;

        // Before the first track of any kind (the arranger, chord, marker, tempo and master
        // tracks come before the song's own).
        auto index = edit.state.getNumChildren();

        for (int i = 0; i < edit.state.getNumChildren(); ++i)
        {
            if (te::TrackList::isTrack (edit.state.getChild (i)))
            {
                index = i;
                break;
            }
        }

        juce::ValueTree node (type);
        edit.state.addChild (node, index, &edit.getUndoManager());
        return node;
    }
}
