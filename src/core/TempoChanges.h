#pragma once

#include "core/Tracktion.h"

/** Tempo changes along the song: from a beat on, the song plays at another tempo, at once or
    gliding there from the change before.

    The first change is the song's tempo, at beat 0 (it can't be moved or removed). Whatever
    changes, clips, notes, automation and markers stay on their beats; audio clips that don't
    follow the tempo keep their speed. Every change goes into the current undo step: the caller
    starts it. Positions are song beats. */
namespace harmonia::core::tempo_changes
{
    struct Change
    {
        double beat = 0.0;
        double bpm = 120.0;
        bool gradual = false;       ///< the tempo glides to it from the change before, over the beats between

        bool operator== (const Change&) const = default;
    };

    /** How close two changes can be. */
    inline constexpr double minGapBeats = 0.25;

    /** Every change, in time order; the first is the song's tempo at beat 0. */
    std::vector<Change> getAll (const te::Edit&);

    /** The change in effect at `beat` (the last one at or before it). */
    int getIndexAt (const te::Edit&, double beat);

    /** The tempo the song plays at there (glides taken into account). */
    double getBpmAt (const te::Edit&, double beat);

    /** A change at `beat`: a new one, or the tempo of the one already there (within the gap).
        Returns its index. */
    int add (te::Edit&, double beat, double bpm);

    void setBpm (te::Edit&, int index, double bpm);

    /** Moves a change (not the first): not past its neighbours, so it keeps its index. */
    void move (te::Edit&, int index, double beat);

    /** Glide to this change from the one before (not for the first), or jump at it. */
    void setGradual (te::Edit&, int index, bool);

    /** Removes a change (not the first): the tempo before it goes on. */
    void remove (te::Edit&, int index);

    /** "bar 9: 90 BPM", "bar 9: glide to 90 BPM". */
    juce::String describe (const te::Edit&, const Change&);
}
