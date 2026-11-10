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

    /** Drops the points put in that change nothing: a repeat of the point before, the first or
        last point holding its neighbour's value, or a point on the straight line between its
        neighbours. */
    void dropNeedlessPoints (std::vector<Point>&);

    /** The line's points, all of them, in place of those it has. Into the current undo step. */
    void setPoints (te::Edit&, te::AutomatableParameter&, const std::vector<Point>&);
}
