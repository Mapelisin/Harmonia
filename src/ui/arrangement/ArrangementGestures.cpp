#include "ui/arrangement/ArrangementGestures.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/Tracks.h"

namespace harmonia::ui::arrangement
{
    namespace clips = core::clips;
    namespace snap = core::snap;

    namespace
    {
        constexpr float dragThreshold = 3.0f;
        constexpr double minClipBeats = 1.0 / 16.0;
    }

    ArrangementGestures::ArrangementGestures (ArrangementModel& m, ArrangementView& v)
        : model (m), view (v)
    {
    }

    snap::Mode ArrangementGestures::getSnapMode (const Pointer& p) const
    {
        return p.mods.isAltDown() ? snap::Mode::none : model.getSnap();
    }

    int ArrangementGestures::trackAt (float y) const
    {
        const auto index = view.yToTrack (y);
        return index >= 0 && index < model.getNumTracks() ? index : -1;
    }

    std::optional<std::pair<DrawnClip, ArrangementGestures::Zone>> ArrangementGestures::hitTest (juce::Point<float> position)
    {
        const auto track = trackAt (position.y);

        if (track < 0)
            return {};

        const auto* d = model.findClipAt (track, view.xToBeat (position.x));

        if (d == nullptr)
            return {};

        const auto left = view.beatToX (d->startBeat), right = view.beatToX (d->endBeat);
        const auto top = view.trackToY (track);
        const auto width = right - left;
        const auto edge = std::min (edgeGrabPixels, width / 4.0f);

        if (width >= menuMarkSize * 2.5f && position.x < left + menuMarkSize && position.y < top + menuMarkSize)
            return std::pair { *d, Zone::menu };

        if (const auto handles = getAudioHandles (*d, view))
        {
            const auto near = [position] (juce::Point<float> centre)
            {
                const auto reach = handleSize / 2.0f + 2.0f;
                return std::abs (position.x - centre.x) <= reach && std::abs (position.y - centre.y) <= reach;
            };

            if (near (handles->gain))       return std::pair { *d, Zone::gain };
            if (near (handles->fadeIn))     return std::pair { *d, Zone::fadeIn };
            if (near (handles->fadeOut))    return std::pair { *d, Zone::fadeOut };
        }

        if (position.x < left + edge)
            return std::pair { *d, Zone::startEdge };

        if (position.x >= right - edge)
            return std::pair { *d, Zone::endEdge };

        return std::pair { *d, Zone::body };
    }

    //==============================================================================
    void ArrangementGestures::down (const Pointer& p)
    {
        downPointer = p;
        lastPosition = p.position;
        dragged = false;
        targets.clear();
        grabbed = nullptr;
        copy = Copy::none;
        toggleOnClick = selectOnlyOnClick = false;
        appliedDelta = 0.0;
        appliedTrackDelta = 0;
        kind = Kind::none;

        if (p.mods.isMiddleButtonDown())
        {
            kind = Kind::pan;
            return;
        }

        if (p.mods.isRightButtonDown())
        {
            kind = Kind::erase;
            model.getProject().beginGesture ("Delete clips");
            eraseAlong (p.position, p.position);
            return;
        }

        if (const auto hit = hitTest (p.position))
        {
            auto* clip = hit->first.clip;
            const auto zone = hit->second;

            if (zone == Zone::menu)
            {
                if (! model.isSelected (*clip))
                    model.setSelection ({ clip });

                model.focusClip (*clip);

                if (onClipMenuRequested != nullptr)
                    onClipMenuRequested (*clip, p.position);

                return;
            }

            model.focusClip (*clip);

            if (zone == Zone::fadeIn || zone == Zone::fadeOut || zone == Zone::gain)
            {
                if (! model.isSelected (*clip))
                    model.setSelection ({ clip });

                auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip);

                if (audioClip == nullptr)
                    return;

                const auto name = zone == Zone::gain ? "Clip gain" : zone == Zone::fadeIn ? "Fade in" : "Fade out";
                model.getProject().beginGesture (name);

                // Double-click: back to none / 0 dB.
                if (p.clicks >= 2)
                {
                    if (zone == Zone::gain)           clips::setGainDb (*audioClip, 0.0f);
                    else if (zone == Zone::fadeIn)    clips::setFadeInBeats (*audioClip, 0.0);
                    else                              clips::setFadeOutBeats (*audioClip, 0.0);
                    return;
                }

                kind = zone == Zone::gain ? Kind::gain : zone == Zone::fadeIn ? Kind::fadeIn : Kind::fadeOut;
                grabbed = clip;

                if (const auto handles = getAudioHandles (hit->first, view))
                    grabbedGainY = handles->gain.y;

                return;
            }

            if (zone == Zone::startEdge || zone == Zone::endEdge)
            {
                if (! model.isSelected (*clip))
                    model.setSelection ({ clip });

                kind = zone == Zone::startEdge ? Kind::resizeStart : Kind::resizeEnd;
                grabbed = clip;
                model.getProject().beginGesture ("Resize clip");
                return;
            }

            beginMove (*clip, p);
            return;
        }

        // Empty space.
        const auto track = trackAt (p.position.y);

        if (p.clicks >= 2 && track >= 0)
        {
            model.createClip (*model.getTrack (track), view.xToBeat (p.position.x));
            return;
        }

        kind = Kind::rectangle;
        const auto adding = p.mods.isShiftDown() || p.mods.isCommandDown();
        selectionAtStart = adding ? model.getSelectedClips() : juce::Array<te::Clip*>();

        if (! adding)
            model.deselectAll();
    }

    void ArrangementGestures::drag (const Pointer& p)
    {
        if (! dragged && p.position.getDistanceFrom (downPointer.position) >= dragThreshold)
            dragged = true;

        switch (kind)
        {
            case Kind::move:        dragMove (p); break;
            case Kind::resizeStart:
            case Kind::resizeEnd:   if (dragged) dragResize (p); break;
            case Kind::fadeIn:
            case Kind::fadeOut:
            case Kind::gain:        if (dragged) dragHandle (p); break;
            case Kind::erase:       eraseAlong (lastPosition, p.position); break;
            case Kind::rectangle:   if (dragged) updateRectangleSelection (p); break;

            case Kind::pan:
                view.scrollByPixels (-(double) (p.position.x - lastPosition.x), -(double) (p.position.y - lastPosition.y));

                if (onViewScrolled != nullptr)
                    onViewScrolled();
                break;

            case Kind::none:
            default:
                break;
        }

        lastPosition = p.position;
    }

    void ArrangementGestures::up (const Pointer& p)
    {
        if (kind == Kind::move && ! dragged)
        {
            if (auto* clip = grabbed.get())
            {
                if (toggleOnClick)
                    model.setSelected (*clip, ! model.isSelected (*clip));
                else if (selectOnlyOnClick)
                    model.setSelection ({ clip });
            }
        }
        else if (kind == Kind::rectangle && dragged)
        {
            updateRectangleSelection (p);
        }

        kind = Kind::none;
        targets.clear();
        grabbed = nullptr;
        selectionAtStart.clear();
    }

    juce::MouseCursor ArrangementGestures::getCursorFor (const Pointer& p)
    {
        if (kind == Kind::resizeStart || kind == Kind::resizeEnd || kind == Kind::fadeIn || kind == Kind::fadeOut)
            return juce::MouseCursor::LeftRightResizeCursor;

        if (kind == Kind::gain)
            return juce::MouseCursor::UpDownResizeCursor;

        if (kind != Kind::none)
            return juce::MouseCursor::NormalCursor;

        if (const auto hit = hitTest (p.position))
        {
            switch (hit->second)
            {
                case Zone::startEdge:
                case Zone::endEdge:
                case Zone::fadeIn:
                case Zone::fadeOut:     return juce::MouseCursor::LeftRightResizeCursor;
                case Zone::gain:        return juce::MouseCursor::UpDownResizeCursor;
                case Zone::menu:        return juce::MouseCursor::PointingHandCursor;
                case Zone::body:
                default:                return juce::MouseCursor::NormalCursor;
            }
        }

        return juce::MouseCursor::NormalCursor;
    }

    std::optional<juce::Rectangle<float>> ArrangementGestures::getSelectionRectangle() const
    {
        if (kind == Kind::rectangle && dragged)
            return juce::Rectangle<float> (downPointer.position, lastPosition);

        return {};
    }

    //==============================================================================
    void ArrangementGestures::beginMove (te::Clip& clip, const Pointer& p)
    {
        kind = Kind::move;
        grabbed = &clip;
        grabbedStart = clips::getStartBeat (clip);

        const auto ctrl = p.mods.isCommandDown();
        copy = p.mods.isShiftDown() ? Copy::linked : ctrl ? Copy::independent : Copy::none;

        // Ctrl+click (without dragging) adds to or removes from the selection; a plain click on
        // a selected clip keeps the selection for dragging and narrows it to the clip on release.
        const auto selected = model.isSelected (clip);
        toggleOnClick = ctrl;
        selectOnlyOnClick = selected && ! ctrl;

        if (! selected && ! ctrl)
            model.setSelection ({ &clip });

        auto moving = model.getSelectedClips();

        if (! moving.contains (&clip))
            moving = { &clip };

        for (auto* c : moving)
            targets.push_back ({ c, clips::getStartBeat (*c), model.indexOf (c->getClipTrack()) });

        model.getProject().beginGesture (copy != Copy::none ? "Copy clips" : "Move clips");
    }

    void ArrangementGestures::dragMove (const Pointer& p)
    {
        if (! dragged || targets.empty())
            return;

        // Dragging, not clicking: the selection stays as it is.
        selectOnlyOnClick = false;
        toggleOnClick = false;

        if (copy != Copy::none)
        {
            makeCopies();
            copy = Copy::none;
        }

        // The grabbed clip's start lands on the grid; the others keep their distance to it.
        const auto rawStart = grabbedStart + (double) (p.position.x - downPointer.position.x) / view.pixelsPerBeat;
        auto delta = snap::snapNearest (rawStart, getSnapMode (p), model.getSnapContext()) - grabbedStart;

        auto earliest = std::numeric_limits<double>::max();
        auto lowestTrack = std::numeric_limits<int>::max(), highestTrack = 0;

        for (const auto& t : targets)
        {
            earliest = std::min (earliest, t.startBeat);
            lowestTrack = std::min (lowestTrack, t.trackIndex);
            highestTrack = std::max (highestTrack, t.trackIndex);
        }

        delta = std::max (delta, -earliest);

        const auto tracks = model.getTracks();
        auto trackDelta = juce::jlimit (-lowestTrack, tracks.size() - 1 - highestTrack,
                                        view.yToTrack (p.position.y) - view.yToTrack (downPointer.position.y));

        // Audio clips only go to audio tracks, MIDI clips only to instrument tracks.
        const auto fits = [&] (int candidate)
        {
            for (const auto& t : targets)
            {
                auto* destination = tracks[t.trackIndex + candidate];

                if (destination == nullptr || core::tracks::isAudio (*destination) != clips::isAudio (*t.clip))
                    return false;
            }

            return true;
        };

        if (! fits (trackDelta))
            trackDelta = appliedTrackDelta;

        if (std::abs (delta - appliedDelta) < 1.0e-9 && trackDelta == appliedTrackDelta)
            return;

        appliedDelta = delta;
        appliedTrackDelta = trackDelta;

        for (const auto& t : targets)
            if (auto* clip = t.clip.get(); clip != nullptr && clips::isInEdit (*clip))
                if (auto* track = tracks[t.trackIndex + trackDelta])
                    clips::move (*clip, t.startBeat + delta, *track);
    }

    void ArrangementGestures::makeCopies()
    {
        juce::Array<te::Clip*> copies;

        for (auto& t : targets)
        {
            auto* original = t.clip.get();

            if (original == nullptr || ! clips::isInEdit (*original))
                continue;

            if (auto* c = clips::duplicate (*original, t.startBeat, copy == Copy::linked))
            {
                if (t.clip == grabbed)
                    grabbed = c;

                t.clip = c;
                copies.add (c);
            }
        }

        // The copy is what the hand holds: it is selected and open.
        model.setSelection (copies);

        if (auto* clip = grabbed.get())
            model.focusClip (*clip);
    }

    void ArrangementGestures::dragResize (const Pointer& p)
    {
        auto* clip = grabbed.get();

        if (clip == nullptr || ! clips::isInEdit (*clip))
            return;

        const auto mode = getSnapMode (p);
        const auto context = model.getSnapContext();
        const auto beat = snap::snapNearest (view.xToBeat (p.position.x), mode, context);
        const auto grid = snap::getGridBeats (mode, context);
        const auto minLength = grid > 0.0 ? std::min (grid, model.getBeatsPerBar()) : minClipBeats;

        if (kind == Kind::resizeStart)
            clips::setStart (*clip, beat, minLength);
        else
            clips::setEnd (*clip, beat, minLength);
    }

    void ArrangementGestures::dragHandle (const Pointer& p)
    {
        auto* clip = dynamic_cast<te::AudioClipBase*> (grabbed.get());

        if (clip == nullptr || ! clips::isInEdit (*clip))
            return;

        if (kind == Kind::gain)
        {
            const auto top = view.trackToY (model.indexOf (clip->getClipTrack()));
            const auto body = juce::Rectangle<float> (0.0f, top + 2.0f + clipHeaderHeight, 1.0f,
                                                      view.trackHeight - 4.0f - clipHeaderHeight);
            const auto db = yToGain (grabbedGainY + (p.position.y - downPointer.position.y), body);

            // Fine steps: 0.1 dB, and a little catch at 0 dB.
            clips::setGainDb (*clip, std::abs (db) < 0.3f ? 0.0f : std::round (db * 10.0f) / 10.0f);
            return;
        }

        // Fades are free (no grid): they follow the sound, not the bars.
        const auto beat = snap::roundToTicks (view.xToBeat (p.position.x));

        if (kind == Kind::fadeIn)
            clips::setFadeInBeats (*clip, beat - clips::getStartBeat (*clip));
        else
            clips::setFadeOutBeats (*clip, clips::getEndBeat (*clip) - beat);
    }

    std::optional<std::pair<juce::String, juce::Point<float>>> ArrangementGestures::getReadout()
    {
        auto* clip = dynamic_cast<te::AudioClipBase*> (grabbed.get());

        if (clip == nullptr || ! clips::isInEdit (*clip) || (kind != Kind::fadeIn && kind != Kind::fadeOut && kind != Kind::gain))
            return {};

        const DrawnClip* drawn = nullptr;

        for (const auto& d : model.getClips())
            if (d.clip == clip)
                drawn = &d;

        if (drawn == nullptr)
            return {};

        const auto handles = getAudioHandles (*drawn, view);

        if (! handles.has_value())
            return {};

        if (kind == Kind::gain)
        {
            const auto db = clips::getGainDb (*clip);
            const auto text = db <= clips::silentGainDb + 0.5f ? juce::String ("Gain: silent")
                                                               : "Gain " + juce::String (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
            return std::pair { text, handles->gain };
        }

        const auto beats = kind == Kind::fadeIn ? clips::getFadeInBeats (*clip) : clips::getFadeOutBeats (*clip);
        const auto seconds = kind == Kind::fadeIn ? clip->getFadeIn().inSeconds() : clip->getFadeOut().inSeconds();
        const auto text = juce::String (kind == Kind::fadeIn ? "Fade in " : "Fade out ") + juce::String (beats, 2)
                        + " beats (" + juce::String (seconds, 2) + " s)";
        return std::pair { text, kind == Kind::fadeIn ? handles->fadeIn : handles->fadeOut };
    }

    std::optional<ArrangementGestures::AudioHandles> ArrangementGestures::getAudioHandles (const DrawnClip& d, const ArrangementView& view)
    {
        auto* clip = dynamic_cast<te::AudioClipBase*> (d.clip);

        if (clip == nullptr)
            return {};

        const auto top = view.trackToY (d.trackIndex);
        const auto r = juce::Rectangle<float>::leftTopRightBottom (view.beatToX (d.startBeat), top + 2.0f,
                                                                   view.beatToX (d.endBeat), top + view.trackHeight - 2.0f);

        if (r.getWidth() < minHandlesWidth)
            return {};

        AudioHandles h;
        h.body = r.withTrimmedTop (clipHeaderHeight);
        h.fadeInX = view.beatToX (d.startBeat + clips::getFadeInBeats (*clip));
        h.fadeOutX = view.beatToX (d.endBeat - clips::getFadeOutBeats (*clip));

        const auto half = handleSize / 2.0f;
        const auto y = h.body.getY() + half + 1.0f;
        h.fadeIn = { juce::jlimit (h.body.getX() + half + 1.0f, h.body.getRight() - half - 1.0f, h.fadeInX), y };
        h.fadeOut = { juce::jlimit (h.body.getX() + half + 1.0f, h.body.getRight() - half - 1.0f, h.fadeOutX), y };
        h.gain = { h.body.getCentreX(), gainToY (clips::getGainDb (*clip), h.body) };
        return h;
    }

    float ArrangementGestures::gainToY (float db, juce::Rectangle<float> body)
    {
        const auto half = handleSize / 2.0f + 1.0f;
        const auto position = juce::jlimit (0.0f, 1.0f, te::decibelsToVolumeFaderPosition (db));
        return body.getBottom() - half - position * std::max (1.0f, body.getHeight() - 2.0f * half);
    }

    float ArrangementGestures::yToGain (float y, juce::Rectangle<float> body)
    {
        const auto half = handleSize / 2.0f + 1.0f;
        const auto position = juce::jlimit (0.0f, 1.0f, (body.getBottom() - half - y) / std::max (1.0f, body.getHeight() - 2.0f * half));

        if (position <= 0.0f)
            return clips::silentGainDb;

        return juce::jlimit (clips::silentGainDb, clips::maxGainDb, te::volumeFaderPositionToDB (position));
    }

    void ArrangementGestures::eraseAlong (juce::Point<float> from, juce::Point<float> to)
    {
        const auto steps = std::max (1, (int) std::ceil (from.getDistanceFrom (to) / 4.0f));
        juce::Array<te::Clip*> hit;

        for (int i = 0; i <= steps; ++i)
        {
            const auto point = from + (to - from) * ((float) i / (float) steps);
            const auto track = trackAt (point.y);

            if (track >= 0)
                if (const auto* d = model.findClipAt (track, view.xToBeat (point.x)))
                    hit.addIfNotAlreadyThere (d->clip);
        }

        model.removeClips (hit);
    }

    void ArrangementGestures::updateRectangleSelection (const Pointer& p)
    {
        const juce::Rectangle<float> area (downPointer.position, p.position);
        auto result = selectionAtStart;

        for (const auto& d : model.getClips())
        {
            const auto top = view.trackToY (d.trackIndex);
            const auto bounds = juce::Rectangle<float>::leftTopRightBottom (view.beatToX (d.startBeat), top,
                                                                            view.beatToX (d.endBeat), top + view.trackHeight);

            if (bounds.intersects (area))
                result.addIfNotAlreadyThere (d.clip);
        }

        model.setSelection (result);
    }
}
