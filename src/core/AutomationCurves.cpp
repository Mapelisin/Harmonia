#include "core/AutomationCurves.h"

namespace harmonia::core::automation_curves
{
    namespace
    {
        constexpr double tolerance = 1.0e-6;

        te::TimePosition toTime (te::Edit& edit, double beat)
        {
            return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
        }

        double toBeat (te::Edit& edit, te::TimePosition time)
        {
            return edit.tempoSequence.toBeats (time).inBeats();
        }

        /** A part of a bent stretch is halved at most this many times over to follow its shape. */
        constexpr int deepestSplit = 4;

        /** One bend for the line from one beat to another, both on one bent stretch of it: the
            bend that takes it through the value the line has halfway. `apart` is the most that
            leaves it away from the line. */
        float fitBend (te::Edit& edit, te::AutomationCurve& curve, double fromBeat, float from, double toBeat, float to,
                       float& apart)
        {
            apart = 0.0f;

            // A stretch only goes one way: level ends are a level part.
            if (from == to)
                return 0.0f;

            const auto at = [&] (double share) { return toTime (edit, fromBeat + share * (toBeat - fromBeat)); };

            // The engine's own curve, of two points, says how each bend goes.
            te::AutomationCurve part (edit, curve.timeBase);
            part.addPoint (at (0.0), from, 0.0f, nullptr);
            part.addPoint (at (1.0), to, 0.0f, nullptr);

            // The more the bend, the later the value changes: the nearer the start's the middle is.
            const auto wanted = curve.getValueAt (at (0.5), 0.0f);
            auto low = -1.0f, high = 1.0f;

            for (int step = 0; step < 20; ++step)
            {
                const auto tried = 0.5f * (low + high);
                part.setCurveValue (0, tried, nullptr);
                const auto value = part.getValueAt (at (0.5), 0.0f);
                const auto tooLate = to > from ? value < wanted : value > wanted;
                (tooLate ? high : low) = tried;
            }

            const auto bend = 0.5f * (low + high);
            part.setCurveValue (0, bend, nullptr);

            for (int i = 1; i < 16; ++i)
                apart = std::max (apart, std::abs (part.getValueAt (at (i / 16.0), 0.0f) - curve.getValueAt (at (i / 16.0), 0.0f)));

            return bend;
        }

        /** The points that follow the line from one beat to another on one bent stretch of it. */
        void followPart (te::Edit& edit, te::AutomationCurve& curve, double fromBeat, float from, double toBeat,
                         float allowed, int depth, std::vector<Point>& points)
        {
            const auto to = curve.getValueAt (toTime (edit, toBeat), 0.0f);
            auto apart = 0.0f;
            const auto bend = fitBend (edit, curve, fromBeat, from, toBeat, to, apart);

            if (apart <= allowed || depth >= deepestSplit)
            {
                points.push_back ({ fromBeat, from, bend, true });
                return;
            }

            // Halved where the line is furthest from the straight way between the part's ends:
            // each half bends half as much. (A bent stretch bends one way, so there is one such
            // place, and it is found by closing in on it.)
            const auto apartFromStraight = [&] (double beat)
            {
                const auto straight = juce::jmap (beat, fromBeat, toBeat, (double) from, (double) to);
                return std::abs (curve.getValueAt (toTime (edit, beat), 0.0f) - straight);
            };

            auto low = fromBeat, high = toBeat;

            for (int step = 0; step < 32; ++step)
            {
                const auto third = (high - low) / 3.0;

                if (apartFromStraight (low + third) < apartFromStraight (high - third))
                    low += third;
                else
                    high -= third;
            }

            const auto middle = 0.5 * (low + high);
            followPart (edit, curve, fromBeat, from, middle, allowed, depth + 1, points);
            followPart (edit, curve, middle, curve.getValueAt (toTime (edit, middle), 0.0f), toBeat, allowed, depth + 1, points);
        }
    }

    std::vector<Point> getPoints (te::Edit& edit, te::AutomatableParameter& parameter)
    {
        auto& curve = parameter.getCurve();
        std::vector<Point> points;

        for (int i = 0; i < curve.getNumPoints(); ++i)
            points.push_back ({ toBeat (edit, curve.getPointTime (i)), curve.getPointValue (i), curve.getPointCurve (i) });

        return points;
    }

    std::vector<Curve> getCurves (te::Edit& edit)
    {
        std::vector<Curve> curves;

        for (auto* item : te::getAllAutomatableEditItems (edit))
        {
            for (auto* parameter : item->getAutomatableParameters())
            {
                auto& curve = parameter->getCurve();

                if (curve.getNumPoints() == 0)
                    continue;

                curves.push_back ({ parameter, getPoints (edit, *parameter) });
            }
        }

        return curves;
    }

    float valueAt (te::Edit& edit, te::AutomatableParameter& parameter, double beat)
    {
        return parameter.getCurve().getValueAt (toTime (edit, beat), 0.0f);
    }

    std::vector<Point> getPart (te::Edit& edit, te::AutomatableParameter& parameter, double beat, double untilBeat)
    {
        auto& curve = parameter.getCurve();
        const auto beatOf = [&] (int index) { return toBeat (edit, curve.getPointTime (index)); };

        // The stretch `beat` is on: from the last point at or before it (after a jump there).
        auto index = -1;

        for (int i = 0; i < curve.getNumPoints() && beatOf (i) <= beat + tolerance; ++i)
            index = i;

        const auto onPoint = index >= 0 && beatOf (index) > beat - tolerance;
        const auto value = onPoint ? curve.getPointValue (index)
                                   : curve.getValueAt (toTime (edit, beat), parameter.getCurrentValue());

        // Before the first point or after the last the line is level.
        if (index < 0 || index + 1 >= curve.getNumPoints())
            return { { beat, value, 0.0f, true } };

        const auto bend = curve.getPointCurve (index);
        const auto endBeat = std::min (untilBeat, beatOf (index + 1));
        const auto wholeStretch = onPoint && endBeat > beatOf (index + 1) - tolerance;

        if (bend == 0.0f || wholeStretch || endBeat - beat <= tolerance)
            return { { beat, value, bend, true } };

        std::vector<Point> points;
        followPart (edit, curve, beat, value, endBeat, shapeTolerance * parameter.getValueRange().getLength(), 0, points);
        return points;
    }

    void appendPart (std::vector<Point>& points, te::Edit& edit, te::AutomatableParameter& parameter, double beat,
                     double untilBeat, bool firstIsAdded, double shiftBeats)
    {
        auto part = getPart (edit, parameter, beat, untilBeat);
        part.front().added = firstIsAdded;

        for (const auto& p : part)
            points.push_back ({ p.beat + shiftBeats, p.value, p.curve, p.added });
    }

    void endAt (std::vector<Point>& points, te::Edit& edit, te::AutomatableParameter& parameter, double cutBeat)
    {
        if (points.empty())
            return;

        const auto last = points.back();
        points.pop_back();
        appendPart (points, edit, parameter, last.beat, cutBeat, last.added);
    }

    void dropNeedlessPoints (std::vector<Point>& points)
    {
        const auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-5f; };

        const auto isNeedless = [&] (size_t i)
        {
            const auto& p = points[i];
            const auto* previous = i > 0 ? &points[i - 1] : nullptr;
            const auto* next = i + 1 < points.size() ? &points[i + 1] : nullptr;

            if (! p.added)
                return false;

            if (next != nullptr && std::abs (next->beat - p.beat) < tolerance && same (next->value, p.value))
                return true;

            if (previous != nullptr && std::abs (previous->beat - p.beat) < tolerance)
                return same (previous->value, p.value);

            if (previous == nullptr)
                return next != nullptr && same (next->value, p.value);

            if (next == nullptr)
                return same (previous->value, p.value);

            return std::abs (next->beat - p.beat) >= tolerance && previous->curve == 0.0f && p.curve == 0.0f
                && same (p.value, (float) juce::jmap (p.beat, previous->beat, next->beat, (double) previous->value,
                                                      (double) next->value));
        };

        // Each point dropped can make its neighbours needless too.
        for (bool dropped = true; dropped;)
        {
            dropped = false;

            for (size_t i = 0; i < points.size(); ++i)
            {
                if (isNeedless (i))
                {
                    // A repeat of the point before: the line goes on from that one, bent as from this.
                    if (i > 0 && std::abs (points[i - 1].beat - points[i].beat) < tolerance
                        && (i + 1 == points.size() || std::abs (points[i + 1].beat - points[i].beat) >= tolerance))
                        points[i - 1].curve = points[i].curve;

                    points.erase (points.begin() + (std::ptrdiff_t) i);
                    dropped = true;
                    break;
                }
            }
        }
    }

    void setPoints (te::Edit& edit, te::AutomatableParameter& parameter, const std::vector<Point>& points)
    {
        auto& curve = parameter.getCurve();
        auto* undoManager = &edit.getUndoManager();

        for (int i = curve.getNumPoints(); --i >= 0;)
            curve.removePoint (i, undoManager);

        // In time order: a point at the same time as another goes after it (a jump).
        for (const auto& p : points)
            curve.addPoint (toTime (edit, p.beat), p.value, p.curve, undoManager);
    }
}
