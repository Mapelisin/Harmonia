#pragma once

#include "core/Tracktion.h"

/** Drum names: which drum each key plays in the General MIDI drum map (the one most drum
    plugins and kits follow), and whether a track shows them in the piano roll. */
namespace harmonia::core::drums
{
    /** "Kick", "Snare", "Closed hat"... for MIDI notes 35 to 81; empty for the others. */
    juce::String getName (int pitch);

    /** A track called "Drums", "Beat", "Perc"...: its piano roll shows drum names unless told otherwise. */
    bool looksLikeDrums (const juce::String& trackName);

    /** Whether the piano roll shows drum names for this track: the user's choice, else by its name. */
    bool showsNames (const te::Track&);

    /** Undoable (the caller starts the gesture). Saved with the project. */
    void setShowsNames (te::Track&, bool);
}
