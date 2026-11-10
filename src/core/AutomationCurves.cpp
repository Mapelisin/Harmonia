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

            return std::abs (next->beat - p.beat) >= tolerance && previous->curve == 0.0f
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
