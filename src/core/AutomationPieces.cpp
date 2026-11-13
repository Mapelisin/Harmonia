#include "core/AutomationPieces.h"

namespace harmonia::core::automation_pieces
{
    using automation_curves::Point;

    namespace
    {
        constexpr double tolerance = 1.0e-6;

        using Lines = juce::Array<te::AutomatableParameter*>;

        /** The lines of the track that have points. */
        Lines getLines (te::AudioTrack& track)
        {
            auto lines = track.getAllAutomatableParams();
            lines.removeIf ([] (te::AutomatableParameter* parameter) { return parameter->getCurve().getNumPoints() == 0; });
            return lines;
        }

        bool isOf (const Piece::Line& line, const te::AutomatableParameter& parameter)
        {
            return parameter.getOwnerID() == line.owner && parameter.paramID == line.parameter;
        }

        /** What these lines of the track do from `startBeat` to `endBeat`. */
        Piece copyLines (te::AudioTrack& track, const Lines& lines, double startBeat, double endBeat)
        {
            auto& edit = track.edit;
            Piece piece { track.itemID, endBeat - startBeat, {} };

            if (piece.lengthBeats <= tolerance)
                return piece;

            for (auto* parameter : lines)
            {
                const auto all = automation_curves::getPoints (edit, *parameter);
                Piece::Line line { parameter->getOwnerID(), parameter->paramID, {} };

                // From the value the line has at the start (after a jump there) to the one it
                // reaches at the end (before a jump there), with its points between. A bent stretch
                // cut by an edge keeps its shape.
                std::vector<Point> inside;

                for (const auto& point : all)
                    if (point.beat > startBeat + tolerance && point.beat < endBeat - tolerance)
                        inside.push_back (point);

                automation_curves::appendPart (line.points, edit, *parameter, startBeat, endBeat, true, -startBeat);

                for (size_t i = 0; i < inside.size(); ++i)
                {
                    if (i + 1 < inside.size())
                        line.points.push_back ({ inside[i].beat - startBeat, inside[i].value, inside[i].curve });
                    else
                        automation_curves::appendPart (line.points, edit, *parameter, inside[i].beat, endBeat, false, -startBeat);
                }

                line.points.push_back ({ piece.lengthBeats, automation_curves::valueAt (edit, *parameter, endBeat), 0.0f, true });
                piece.lines.push_back (std::move (line));
            }

            return piece;
        }

        /** The line runs straight from the value it has at `startBeat` to the one it has at `endBeat`. */
        void clearLine (te::Edit& edit, te::AutomatableParameter& parameter, double startBeat, double endBeat)
        {
            const auto all = automation_curves::getPoints (edit, parameter);
            const auto hasPointsThere = std::any_of (all.begin(), all.end(), [startBeat, endBeat] (const Point& point)
            {
                return point.beat > startBeat - tolerance && point.beat < endBeat + tolerance;
            });

            // Already one run across the stretch, with nothing in it to take out.
            if (! hasPointsThere)
                return;

            std::vector<Point> points;

            for (const auto& point : all)
                if (point.beat < startBeat - tolerance)
                    points.push_back (point);

            // The stretches the edges cut keep their shape outside.
            automation_curves::endAt (points, edit, parameter, startBeat);
            const Point atStart { startBeat, automation_curves::valueAt (edit, parameter, startBeat), 0.0f, true };
            points.push_back (atStart);
            automation_curves::appendPart (points, edit, parameter, endBeat, std::numeric_limits<double>::max(), true);

            for (const auto& point : all)
                if (point.beat > endBeat + tolerance)
                    points.push_back (point);

            automation_curves::dropNeedlessPoints (points);

            // A line all of whose points were in the stretch stays a line (flat): with no points
            // at all it would be where its knob is instead.
            if (points.empty())
                points.push_back ({ atStart.beat, atStart.value, 0.0f });

            automation_curves::setPoints (edit, parameter, points);
        }
    }

    Piece copy (te::AudioTrack& track, double startBeat, double endBeat)
    {
        return copyLines (track, getLines (track), startBeat, endBeat);
    }

    Piece copyOwn (te::AudioTrack& track, double startBeat, double endBeat)
    {
        auto lines = getLines (track);

        lines.removeIf ([&] (te::AutomatableParameter* parameter)
        {
            const auto points = automation_curves::getPoints (track.edit, *parameter);

            // A point at the clip's end starts what comes after the clip.
            return std::none_of (points.begin(), points.end(), [startBeat, endBeat] (const Point& point)
            {
                return point.beat > startBeat - tolerance && point.beat < endBeat - tolerance;
            });
        });

        return copyLines (track, lines, startBeat, endBeat);
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
                return isOf (line, *parameter);
            });

            if (found == parameters.end())
                continue;

            auto& parameter = **found;
            const auto all = automation_curves::getPoints (edit, parameter);

            // A line with no points left is flat at where its knob is.
            const auto before = all.empty() ? parameter.getCurrentValue() : automation_curves::valueAt (edit, parameter, startBeat);
            std::vector<Point> points;

            for (const auto& point : all)
                if (point.beat < startBeat - tolerance)
                    points.push_back (point);

            // The stretches the piece's edges cut keep their shape outside it.
            automation_curves::endAt (points, edit, parameter, startBeat);
            points.push_back ({ startBeat, before, 0.0f, true });

            for (const auto& point : line.points)
                points.push_back ({ startBeat + point.beat, point.value, point.curve, point.added });

            automation_curves::appendPart (points, edit, parameter, endBeat, std::numeric_limits<double>::max(), true);

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

        for (auto* parameter : getLines (track))
            clearLine (track.edit, *parameter, startBeat, endBeat);
    }

    void clear (te::AudioTrack& track, const Piece& piece, double startBeat)
    {
        if (track.itemID != piece.track || piece.lengthBeats <= tolerance)
            return;

        for (auto* parameter : getLines (track))
        {
            const auto isIts = std::any_of (piece.lines.begin(), piece.lines.end(), [parameter] (const Piece::Line& line)
            {
                return isOf (line, *parameter);
            });

            if (isIts)
                clearLine (track.edit, *parameter, startBeat, startBeat + piece.lengthBeats);
        }
    }

    void carry (const std::vector<Carried>& clips, bool copied)
    {
        std::vector<Piece> pieces;

        for (const auto& clip : clips)
            pieces.push_back (clip.track != nullptr ? copyOwn (*clip.track, clip.fromBeat, clip.fromBeat + clip.lengthBeats) : Piece());

        if (! copied)
            for (size_t i = 0; i < clips.size(); ++i)
                if (! pieces[i].isEmpty())
                    clear (*clips[i].track, pieces[i], clips[i].fromBeat);

        for (size_t i = 0; i < clips.size(); ++i)
            if (! pieces[i].isEmpty())
                paste (*clips[i].track, pieces[i], clips[i].toBeat);
    }
}
