#include "core/AutomationPieces.h"

namespace harmonia::core::automation_pieces
{
    using automation_curves::Point;

    namespace
    {
        constexpr double tolerance = 1.0e-6;

        bool isAt (const Point& point, double beat)
        {
            return std::abs (point.beat - beat) < tolerance;
        }

        /** The value a line has at `beat` once any jump there is over. */
        float valueAfter (te::Edit& edit, te::AutomatableParameter& parameter, const std::vector<Point>& points, double beat)
        {
            for (auto point = points.rbegin(); point != points.rend(); ++point)
                if (isAt (*point, beat))
                    return point->value;

            return automation_curves::valueAt (edit, parameter, beat);
        }

        /** The lines of the track that have points. */
        juce::Array<te::AutomatableParameter*> getLines (te::AudioTrack& track)
        {
            auto lines = track.getAllAutomatableParams();
            lines.removeIf ([] (te::AutomatableParameter* parameter) { return parameter->getCurve().getNumPoints() == 0; });
            return lines;
        }
    }

    Piece copy (te::AudioTrack& track, double startBeat, double endBeat)
    {
        auto& edit = track.edit;
        Piece piece { track.itemID, endBeat - startBeat, {} };

        if (piece.lengthBeats <= tolerance)
            return piece;

        for (auto* parameter : getLines (track))
        {
            const auto all = automation_curves::getPoints (edit, *parameter);
            Piece::Line line { parameter->getOwnerID(), parameter->paramID, {} };

            // From the value the line has at the start (after a jump there) to the one it
            // reaches at the end (before a jump there), with its points between.
            line.points.push_back ({ 0.0, valueAfter (edit, *parameter, all, startBeat), 0.0f, true });

            for (const auto& point : all)
                if (point.beat > startBeat + tolerance && point.beat < endBeat - tolerance)
                    line.points.push_back ({ point.beat - startBeat, point.value, point.curve });

            line.points.push_back ({ piece.lengthBeats, automation_curves::valueAt (edit, *parameter, endBeat), 0.0f, true });
            piece.lines.push_back (std::move (line));
        }

        return piece;
    }

    void paste (te::AudioTrack& track, const Piece& piece, double startBeat)
    {
        if (track.itemID != piece.track || piece.lengthBeats <= tolerance)
            return;

        auto& edit = track.edit;
        const auto endBeat = startBeat + piece.lengthBeats;
        const auto parameters = track.getAllAutomatableParams();

        for (const auto& line : piece.lines)
        {
            const auto found = std::find_if (parameters.begin(), parameters.end(), [&line] (te::AutomatableParameter* parameter)
            {
                return parameter->getOwnerID() == line.owner && parameter->paramID == line.parameter;
            });

            if (found == parameters.end())
                continue;

            auto& parameter = **found;
            const auto all = automation_curves::getPoints (edit, parameter);

            // A line with no points left is flat at where its knob is.
            const auto before = all.empty() ? parameter.getCurrentValue() : automation_curves::valueAt (edit, parameter, startBeat);
            const auto after = all.empty() ? parameter.getCurrentValue() : valueAfter (edit, parameter, all, endBeat);

            std::vector<Point> points;

            for (const auto& point : all)
                if (point.beat < startBeat - tolerance)
                    points.push_back (point);

            points.push_back ({ startBeat, before, 0.0f, true });

            for (const auto& point : line.points)
                points.push_back ({ startBeat + point.beat, point.value, point.curve, point.added });

            points.push_back ({ endBeat, after, 0.0f, true });

            for (const auto& point : all)
                if (point.beat > endBeat + tolerance)
                    points.push_back (point);

            automation_curves::dropNeedlessPoints (points);
            automation_curves::setPoints (edit, parameter, points);
        }
    }

    void clear (te::AudioTrack& track, double startBeat, double endBeat)
    {
        if (endBeat - startBeat <= tolerance)
            return;

        auto& edit = track.edit;

        for (auto* parameter : getLines (track))
        {
            const auto all = automation_curves::getPoints (edit, *parameter);
            const auto hasPointsThere = std::any_of (all.begin(), all.end(), [startBeat, endBeat] (const Point& point)
            {
                return point.beat > startBeat - tolerance && point.beat < endBeat + tolerance;
            });

            // Already one straight run across the stretch.
            if (! hasPointsThere)
                continue;

            std::vector<Point> points;

            for (const auto& point : all)
                if (point.beat < startBeat - tolerance)
                    points.push_back (point);

            const Point atStart { startBeat, automation_curves::valueAt (edit, *parameter, startBeat), 0.0f, true };
            points.push_back (atStart);
            points.push_back ({ endBeat, valueAfter (edit, *parameter, all, endBeat), 0.0f, true });

            for (const auto& point : all)
                if (point.beat > endBeat + tolerance)
                    points.push_back (point);

            automation_curves::dropNeedlessPoints (points);

            // A line all of whose points were in the stretch stays a line (flat): with no points
            // at all it would be where its knob is instead.
            if (points.empty())
                points.push_back ({ atStart.beat, atStart.value, 0.0f });

            automation_curves::setPoints (edit, *parameter, points);
        }
    }
}
