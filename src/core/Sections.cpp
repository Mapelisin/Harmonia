#include "core/Sections.h"

#include "core/AutomationCurves.h"
#include "core/Clips.h"
#include "core/Markers.h"

namespace harmonia::core::sections
{
    using namespace automation_curves;

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
        void splitClipsAt (te::Edit& edit, double beat)
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

        /** The clips that lie within the stretch (after splitClipsAt its edges). */
        juce::Array<te::Clip*> getClipsWithin (te::Edit& edit, double startBeat, double endBeat)
        {
            juce::Array<te::Clip*> within;

            for (auto* track : te::getAudioTracks (edit))
                for (auto* clip : track->getClips())
                    if (clips::getStartBeat (*clip) >= startBeat - tolerance && clips::getEndBeat (*clip) <= endBeat + tolerance)
                        within.add (clip);

            return within;
        }
    }

    void splitAt (te::Edit& edit, double beat)
    {
        splitClipsAt (edit, beat);
    }

    void insertSpace (te::Edit& edit, double startBeat, double lengthBeats)
    {
        startBeat = std::max (0.0, startBeat);

        if (lengthBeats <= tolerance)
            return;

        splitClipsAt (edit, startBeat);

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

        splitClipsAt (edit, startBeat);
        splitClipsAt (edit, endBeat);

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

        splitClipsAt (edit, startBeat);
        splitClipsAt (edit, endBeat);

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
