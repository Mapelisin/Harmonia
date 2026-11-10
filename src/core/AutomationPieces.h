#pragma once

#include "core/AutomationCurves.h"

/** What the automation lines of a track do over a stretch of time, taken out to go along with a
    piece of time that is moved, copied or pasted on that track (the clips go by the arrangement's
    own means). Every change goes into the current undo step: the caller begins it.

    A piece keeps no hold on the song it was copied from (it may be pasted after that song has
    changed, or closed): it knows its lines by their plugin and parameter, and goes back only onto
    the track it came from, onto the lines that are still there. */
namespace harmonia::core::automation_pieces
{
    struct Piece
    {
        struct Line
        {
            te::EditItemID owner;                           // the plugin (or other item) whose parameter it is
            juce::String parameter;                         // the parameter's ID in it
            std::vector<automation_curves::Point> points;   // beats from the start of the stretch: the first at 0, the last at its end
        };

        te::EditItemID track;
        double lengthBeats = 0.0;
        std::vector<Line> lines;

        bool isEmpty() const noexcept   { return lines.empty(); }
    };

    /** What the lines of the track (those that have points) do from `startBeat` to `endBeat`. */
    Piece copy (te::AudioTrack&, double startBeat, double endBeat);

    /** Puts the piece at `startBeat` on the track it was copied from, in place of what its lines
        do there. Outside the stretch the lines keep their shape (jumping at an edge, if they
        must). Another track, or a line that isn't there any more: left alone. */
    void paste (te::AudioTrack&, const Piece&, double startBeat);

    /** Where a piece of time was taken from: each line of the track runs straight from the value
        it has at `startBeat` to the one it has at `endBeat`. */
    void clear (te::AudioTrack&, double startBeat, double endBeat);
}
