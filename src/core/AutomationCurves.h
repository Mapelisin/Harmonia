#pragma once

#include "core/Tracktion.h"

/** Automation lines read and written whole, their points in beats: what the commands that move
    time about work with (core::sections: bars on every track; core::automation_pieces: a piece of
    time on some tracks). Values are the parameter's own, not 0..1. */
namespace harmonia::core::automation_curves
{
    struct Point
    {
        double beat;
        float value, curve;
        bool added = false;     // put in to keep the shape, not one of the user's
    };

    struct Curve
    {
        te::AutomatableParameter::Ptr parameter;
        std::vector<Point> points;
    };

    /** Every automation curve that has points, with its points in beats. */
    std::vector<Curve> getCurves (te::Edit&);

    /** The points of one line, in beats. */
    std::vector<Point> getPoints (te::Edit&, te::AutomatableParameter&);

    /** The value the curve reaches at `beat` (before a jump there, if it jumps there). */
    float valueAt (te::Edit&, te::AutomatableParameter&, double beat);

    /** How near a cut bent stretch stays to the shape it had, as a share of the parameter's range. */
    constexpr float shapeTolerance = 0.008f;

    /** The points that draw the line again from `beat` on, up to its next point or to `untilBeat`
        where that comes first: what is put there when the line is cut at those beats, so that it
        keeps its shape. The first is at `beat`, with the value the line has there (after a jump
        there) and the bend it goes on with. A part of a bent stretch is not always one bend:
        where one can't follow it (see shapeTolerance), more points are put along it. The point
        that ends the part is not among them. All are `added`. */
    std::vector<Point> getPart (te::Edit&, te::AutomatableParameter&, double beat,
                                double untilBeat = std::numeric_limits<double>::max());

    /** getPart put at the end of `points`, `shiftBeats` later; its first point `added` as told. */
    void appendPart (std::vector<Point>& points, te::Edit&, te::AutomatableParameter&, double beat, double untilBeat,
                     bool firstIsAdded, double shiftBeats = 0.0);

    /** `points` being the line's own points before `cutBeat`: the last of them goes on up to the
        cut as the line did (see getPart). */
    void endAt (std::vector<Point>& points, te::Edit&, te::AutomatableParameter&, double cutBeat);

    /** Drops the points put in that change nothing: a repeat of the point before, the first or
        last point holding its neighbour's value, or a point on the straight line between its
        neighbours. */
    void dropNeedlessPoints (std::vector<Point>&);

    /** The line's points, all of them, in place of those it has. Into the current undo step. */
    void setPoints (te::Edit&, te::AutomatableParameter&, const std::vector<Point>&);
}
