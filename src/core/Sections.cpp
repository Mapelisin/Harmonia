#include "core/Sections.h"

#include "core/Clips.h"
#include "core/Markers.h"

namespace harmonia::core::sections
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

        /** Splits every clip across `beat`, so none crosses it (each half of a looping clip keeps
            showing its first time round, as the arrangement's split does). */
        void splitAt (te::Edit& edit, double beat)
        {
            for (auto* track : te::getAudioTracks (edit))
            {
                juce::Array<te::Clip*> across;

                for (auto* clip : track->getClips())
                    if (clips::getStartBeat (*clip) < beat - tolerance && clips::getEndBeat (*clip) > beat + tolerance)
                        across.add (clip);

                for (auto* clip : across)
                    clips::split (*clip, beat);
            }
        }

        /** The clips that lie within the stretch (after splitAt its edges). */
        juce::Array<te::Clip*> getClipsWithin (te::Edit& edit, double startBeat, double endBeat)
        {
            juce::Array<te::Clip*> within;

            for (auto* track : te::getAudioTracks (edit))
                for (auto* clip : track->getClips())
                    if (clips::getStartBeat (*clip) >= startBeat - tolerance && clips::getEndBeat (*clip) <= endBeat + tolerance)
                        within.add (clip);

            return within;
        }

        //==============================================================================
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

                    Curve c { parameter, {} };

                    for (int i = 0; i < curve.getNumPoints(); ++i)
                        c.points.push_back ({ toBeat (edit, curve.getPointTime (i)), curve.getPointValue (i), curve.getPointCurve (i) });

                    curves.push_back (std::move (c));
                }
            }

            return curves;
        }

        /** The value the curve reaches at `beat` (before a jump there, if it jumps there). */
        float valueAt (te::Edit& edit, te::AutomatableParameter& parameter, double beat)
        {
            return parameter.getCurve().getValueAt (toTime (edit, beat), 0.0f);
        }

        /** Drops the points put in that change nothing: a repeat of the point before, the first or
            last point holding its neighbour's value, or a point on the straight line between its
            neighbours. */
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

    void insertSpace (te::Edit& edit, double startBeat, double lengthBeats)
    {
        startBeat = std::max (0.0, startBeat);

        if (lengthBeats <= tolerance)
            return;

        splitAt (edit, startBeat);

        // As te::insertSpaceIntoEditFromBeatRange, but moving every clip: Tracktion's own clip
        // move stops at the first clip before the point, and a track's clips are only put back in
        // time order a moment after they change.
        const auto time = toTime (edit, startBeat);
        const auto& tempo = edit.tempoSequence.getTempoAt (time - te::TimeDuration::fromSeconds (1.0e-4));
        const auto length = te::TimeDuration::fromSeconds (lengthBeats * tempo.getApproxBeatLength().inSeconds());

        for (auto* track : te::getAudioTracks (edit))
        {
            juce::Array<te::Clip*> later;

            for (auto* clip : track->getClips())
                if (clip->getPosition().time.getCentre() >= time)
                    later.add (clip);

            for (auto* clip : later)
                clip->setStart (clip->getPosition().getStart() + length, false, true);
        }

        // Automation holds its value there through the new space.
        for (auto* item : te::getAllAutomatableEditItems (edit))
            for (auto* parameter : item->getAutomatableParameters())
                parameter->getCurve().insertSpace (*parameter, time, length);

        edit.pitchSequence.insertSpaceIntoSequence (time, length, false);
        edit.tempoSequence.insertSpaceIntoSequence (time, length, false);

        for (const auto& marker : markers::getAll (edit))
            if (marker.beat >= startBeat - tolerance)
                markers::move (edit, marker, marker.beat + lengthBeats);
    }

    void deleteRange (te::Edit& edit, double startBeat, double endBeat)
    {
        startBeat = std::max (0.0, startBeat);
        const auto length = endBeat - startBeat;

        if (length <= tolerance)
            return;

        splitAt (edit, startBeat);
        splitAt (edit, endBeat);

        // Taken out here so their link groups are kept tidy; Tracktion closes the gap.
        clips::remove (getClipsWithin (edit, startBeat, endBeat));

        for (const auto& marker : markers::getAll (edit))
        {
            if (marker.beat >= endBeat - tolerance)
                markers::move (edit, marker, marker.beat - length);
            else if (marker.beat >= startBeat - tolerance)
                markers::remove (edit, marker);
        }

        te::deleteRegionOfTracks (edit, te::TimeRange (toTime (edit, startBeat), toTime (edit, endBeat)), false,
                                  te::CloseGap::yes, nullptr);
    }

    juce::Array<te::Clip*> duplicateRange (te::Edit& edit, double startBeat, double endBeat)
    {
        juce::Array<te::Clip*> copies;
        startBeat = std::max (0.0, startBeat);
        const auto length = endBeat - startBeat;

        if (length <= tolerance)
            return copies;

        splitAt (edit, startBeat);
        splitAt (edit, endBeat);

        // What the stretch holds, before making room for its copy moves the rest along.
        std::vector<te::Clip::Ptr> inside;

        for (auto* clip : getClipsWithin (edit, startBeat, endBeat))
            inside.push_back (clip);

        std::vector<markers::Marker> markersInside;

        for (const auto& marker : markers::getAll (edit))
            if (marker.beat >= startBeat - tolerance && marker.beat < endBeat - tolerance)
                markersInside.push_back (marker);

        // Each curve played twice over the stretch: as it was up to the stretch's end, then the
        // stretch again (from its value at the start), then the rest later. The points at the
        // edges keep the shape where a curve runs across them.
        std::vector<Curve> curves;

        for (auto& curve : getCurves (edit))
        {
            // The values the curve reaches at the edges (before a jump there, if it jumps there).
            const auto atStart = valueAt (edit, *curve.parameter, startBeat);
            const auto atEnd = valueAt (edit, *curve.parameter, endBeat);
            const auto pointAtStart = std::any_of (curve.points.begin(), curve.points.end(),
                                                   [startBeat] (const Point& p) { return std::abs (p.beat - startBeat) < tolerance; });
            std::vector<Point> points;

            for (const auto& p : curve.points)
                if (p.beat < endBeat - tolerance)
                    points.push_back (p);

            // The stretch ends as it did; its copy starts as it started (with the points there).
            points.push_back ({ endBeat, atEnd, 0.0f, true });

            if (! pointAtStart)
                points.push_back ({ endBeat, atStart, 0.0f, true });

            for (const auto& p : curve.points)
                if (p.beat > startBeat - tolerance && p.beat < endBeat - tolerance)
                    points.push_back ({ p.beat + length, p.value, p.curve });

            // The copy ends as the stretch did; then what came after (from the points at the end).
            points.push_back ({ endBeat + length, atEnd, 0.0f, true });

            for (const auto& p : curve.points)
                if (p.beat >= endBeat - tolerance)
                    points.push_back ({ p.beat + length, p.value, p.curve });

            dropNeedlessPoints (points);
            curves.push_back ({ curve.parameter, std::move (points) });
        }

        insertSpace (edit, endBeat, length);

        for (auto& clip : inside)
            if (auto* copy = clips::duplicate (*clip, clips::getStartBeat (*clip) + length, true))
                copies.add (copy);

        for (const auto& marker : markersInside)
            markers::add (edit, marker.beat + length, marker.name);

        for (const auto& curve : curves)
            setPoints (edit, *curve.parameter, curve.points);

        return copies;
    }
}
