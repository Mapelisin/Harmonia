#pragma once

#include "core/Tracktion.h"

/** The song's nodes for data of Harmonia's own (track folders, markers, MIDI controls). */
namespace harmonia::core::song_nodes
{
    /** The child of the song's state of this type, made (into the current undo step) if it isn't
        there yet: before the tracks, where the engine keeps everything that isn't a track.
        Appended after them it would stay there only until the engine next sorts the song's nodes,
        which it does a moment after a track appears: after an undo that brings a track back, that
        move was an undo step of its own, and the redo steps were gone. */
    juce::ValueTree getOrCreate (te::Edit&, const juce::Identifier& type);
}
