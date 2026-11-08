#pragma once

#include "core/Tracktion.h"

/** Song markers: named places in the song ("Verse", "Chorus") shown on the arrangement's ruler.
    Kept in the project (a child of the edit's state) and changed with undo; the caller starts
    the gesture. Positions are song beats. */
namespace harmonia::core::markers
{
    struct Marker
    {
        juce::ValueTree state;          // where it is kept: identifies the marker
        double beat = 0.0;
        juce::String name;
    };

    /** All markers in time order. */
    std::vector<Marker> getAll (const te::Edit&);

    /** Adds a marker (at least beat 0) and returns it. */
    Marker add (te::Edit&, double beat, const juce::String& name);

    void move (te::Edit&, const Marker&, double beat);
    void rename (te::Edit&, const Marker&, const juce::String& name);
    void remove (te::Edit&, const Marker&);

    /** The marker at or before `beat` (and, for `after`, the first one after it). */
    std::optional<Marker> findAtOrBefore (const te::Edit&, double beat);
    std::optional<Marker> findAfter (const te::Edit&, double beat);

    /** The section a marker starts: up to the next marker, or else `songEndBeat` (if later). */
    std::pair<double, double> getSection (const te::Edit&, const Marker&, double songEndBeat);

    /** Names offered when adding a marker. */
    const juce::StringArray& getSuggestedNames();

    /** True for the tree that keeps the markers, or anything in it (to know what changed). */
    bool isMarkerTree (const juce::ValueTree&);
}
