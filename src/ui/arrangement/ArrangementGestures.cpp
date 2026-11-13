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
                const auto reach = audio_handles::size / 2.0f + 2.0f;
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
        copied = false;
        slip.reset();
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

        const auto hit = hitTest (p.position);
        const auto onNameStrip = hit.has_value() && p.position.y < view.trackToY (hit->first.trackIndex) + clipHeaderHeight;

        // Inside the selected piece of time (not on a clip's name strip, edges or handles): the
        // piece is taken, to be moved.
        if (const auto piece = model.getTimeSelection();
            piece.has_value() && p.clicks < 2 && (! hit.has_value() || (hit->second == Zone::body && ! onNameStrip))
              && piece->contains (view.xToBeat (p.position.x), trackAt (p.position.y)))
        {
            kind = Kind::moveTime;
            movingPiece = piece;
            moveBeats = 0.0;
            moveRows = 0;
            return;
        }

        if (hit.has_value())
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

            // The clip's name strip takes the clip; the rest of it is time, as the empty rows are -
            // but with Ctrl+Shift the sound of a clip is taken there, to be slid under the clip.
            if (onNameStrip)
                beginMove (*clip, p);
            else if (! (p.mods.isCommandDown() && p.mods.isShiftDown() && beginSlip (*clip, hit->first.trackIndex)))
                beginTime (p, clip);

            return;
        }

        // Empty space.
        const auto track = trackAt (p.position.y);

        if (p.clicks >= 2 && track >= 0)
        {
            // Not into a collapsed folder (which of its tracks?), nor onto a frozen track.
            if (! model.isLockedRow (model.getTrack (track)))
                model.createClip (*model.getTrack (track), view.xToBeat (p.position.x));

            return;
        }

        // Shift or Ctrl: whole clips, with a rectangle (added to the selection). Else: time.
        if (p.mods.isShiftDown() || p.mods.isCommandDown())
        {
            kind = Kind::rectangle;
            selectionAtStart = model.getSelectedClips();
            return;
        }

        beginTime (p, nullptr);
    }

    void ArrangementGestures::beginTime (const Pointer& p, te::Clip* clipUnderIt)
    {
        kind = Kind::timeSelect;
        clickedClip = clipUnderIt;

        // Without a drag it is a click: on a clip, Ctrl adds it to the selection or takes it out.
        toggleOnClick = clipUnderIt != nullptr && p.mods.isCommandDown();
    }

    void ArrangementGestures::dragTime (const Pointer& p)
    {
        const auto rows = model.getNumTracks();

        if (rows == 0)
            return;

        const auto mode = getSnapMode (p);
        const auto context = model.getSnapContext();
        model.setTimeSelection (snap::snapNearest (view.xToBeat (downPointer.position.x), mode, context),
                                snap::snapNearest (view.xToBeat (p.position.x), mode, context),
                                juce::jlimit (0, rows - 1, view.yToTrack (downPointer.position.y)),
                                juce::jlimit (0, rows - 1, view.yToTrack (p.position.y)));
    }

    void ArrangementGestures::dragPiece (const Pointer& p)
    {
        if (! movingPiece.has_value())
            return;

        // Its start lands on the grid; never before the song, never off the rows.
        const auto raw = movingPiece->startBeat + (double) (p.position.x - downPointer.position.x) / view.pixelsPerBeat;
        moveBeats = std::max (-movingPiece->startBeat,
                              snap::snapNearest (raw, getSnapMode (p), model.getSnapContext()) - movingPiece->startBeat);
        moveRows = juce::jlimit (-movingPiece->firstRow, std::max (-movingPiece->firstRow, model.getNumTracks() - 1 - movingPiece->lastRow),
                                 view.yToTrack (p.position.y) - view.yToTrack (downPointer.position.y));
    }

    std::optional<ArrangementModel::TimeSelection> ArrangementGestures::getTimeMoveTarget() const
    {
        if (kind != Kind::moveTime || ! dragged || ! movingPiece.has_value())
            return std::nullopt;

        auto target = *movingPiece;
        target.startBeat += moveBeats;
        target.endBeat += moveBeats;
        target.firstRow += moveRows;
        target.lastRow += moveRows;
        return target;
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
            case Kind::timeSelect:  if (dragged) dragTime (p); break;
            case Kind::moveTime:    if (dragged) dragPiece (p); break;
            case Kind::slip:        if (dragged) dragSlip (p); break;

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
        else if (kind == Kind::move)
        {
            carryAutomation();
        }
        else if (kind == Kind::rectangle && dragged)
        {
            updateRectangleSelection (p);
        }
        else if (kind == Kind::timeSelect)
        {
            if (dragged)
            {
                dragTime (p);
            }
            else if (auto* clip = clickedClip.get())
            {
                // A click on a clip: it is selected (Ctrl: added or taken out), no time is.
                model.clearTimeSelection();

                if (toggleOnClick)
                    model.setSelected (*clip, ! model.isSelected (*clip));
                else
                    model.setSelection ({ clip });
            }
            else
            {
                model.deselectAll();
            }
        }
        else if (kind == Kind::moveTime)
        {
            if (dragged && movingPiece.has_value())
            {
                dragPiece (p);

                if (const auto problem = model.moveTime (moveBeats, moveRows, p.mods.isCommandDown());
                    problem.isNotEmpty() && onProblem != nullptr)
                    onProblem (problem);
            }
            else
            {
                // A click inside the piece lets it go; on a clip, it is a click on the clip.
                model.clearTimeSelection();

                if (const auto hit = hitTest (p.position))
                {
                    model.focusClip (*hit->first.clip);
                    model.setSelection ({ hit->first.clip });
                }
            }
        }

        kind = Kind::none;
        targets.clear();
        grabbed = nullptr;
        slip.reset();
        selectionAtStart.clear();
        clickedClip = nullptr;
        movingPiece.reset();
    }

    juce::MouseCursor ArrangementGestures::getCursorFor (const Pointer& p)
    {
        if (kind == Kind::resizeStart || kind == Kind::resizeEnd || kind == Kind::fadeIn || kind == Kind::fadeOut)
            return juce::MouseCursor::LeftRightResizeCursor;

        if (kind == Kind::gain)
            return juce::MouseCursor::UpDownResizeCursor;

        if (kind == Kind::timeSelect)
            return juce::MouseCursor::IBeamCursor;

        if (kind == Kind::moveTime)
            return juce::MouseCursor::DraggingHandCursor;

        if (kind != Kind::none)
            return juce::MouseCursor::NormalCursor;

        const auto hit = hitTest (p.position);
        const auto row = trackAt (p.position.y);
        const auto onNameStrip = hit.has_value() && p.position.y < view.trackToY (hit->first.trackIndex) + clipHeaderHeight;

        if (hit.has_value() && hit->second != Zone::body)
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
                default:                break;
            }
        }

        // A clip's name strip takes the clip; the selected piece of time can be taken; anywhere
        // else in the rows a drag selects time.
        if (onNameStrip)
            return juce::MouseCursor::NormalCursor;

        if (const auto piece = model.getTimeSelection(); piece.has_value() && piece->contains (view.xToBeat (p.position.x), row))
            return juce::MouseCursor::DraggingHandCursor;

        return row >= 0 ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor;
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
            copied = true;
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

                if (destination == nullptr || model.isLockedRow (destination)
                     || core::tracks::isAudio (*destination) != clips::isAudio (*t.clip))
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

    bool ArrangementGestures::beginSlip (te::Clip& clip, int trackIndex)
    {
        auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip);

        if (wave == nullptr || ! core::audio_tempo::canSlide (*wave) || model.isLockedRow (model.getTrack (trackIndex)))
            return false;

        if (! model.isSelected (clip))
            model.setSelection ({ &clip });

        model.getProject().beginGesture ("Slide the sound");
        slip.emplace (*wave);
        kind = Kind::slip;
        return true;
    }

    void ArrangementGestures::dragSlip (const Pointer& p)
    {
        if (! slip.has_value())
            return;

        // With the mouse, by steps of the grid (Alt: freely), as far as the file goes.
        const auto mode = getSnapMode (p);
        const auto context = model.getSnapContext();
        slip->moveTo (snap::snapNearest (view.xToBeat (p.position.x), mode, context)
                        - snap::snapNearest (view.xToBeat (downPointer.position.x), mode, context));
    }

    void ArrangementGestures::carryAutomation()
    {
        // Along their own rows the clips take their automation with them, once they are let go
        // (another row has other knobs).
        if (appliedTrackDelta != 0 || std::abs (appliedDelta) < 1.0e-9)
            return;

        std::vector<core::automation_pieces::Carried> carried;

        for (const auto& t : targets)
            if (auto* clip = t.clip.get(); clip != nullptr && clips::isInEdit (*clip))
                if (auto* track = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
                    carried.push_back ({ track, t.startBeat, t.startBeat + appliedDelta,
                                         clips::getEndBeat (*clip) - clips::getStartBeat (*clip) });

        model.carryAutomation (carried, copied);
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
            const auto db = audio_handles::yToGain (grabbedGainY + (p.position.y - downPointer.position.y), body);
            clips::setGainDb (*clip, audio_handles::roundDraggedGain (db));
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
            return std::pair { audio_handles::describeGain (clips::getGainDb (*clip)), handles->gain };

        return std::pair { audio_handles::describeFade (*clip, kind == Kind::fadeIn), kind == Kind::fadeIn ? handles->fadeIn : handles->fadeOut };
    }

    std::optional<audio_handles::Handles> ArrangementGestures::getAudioHandles (const DrawnClip& d, const ArrangementView& view)
    {
        auto* clip = dynamic_cast<te::AudioClipBase*> (d.clip);

        if (clip == nullptr)
            return {};

        const auto top = view.trackToY (d.trackIndex);
        const auto r = juce::Rectangle<float>::leftTopRightBottom (view.beatToX (d.startBeat), top + 2.0f,
                                                                   view.beatToX (d.endBeat), top + view.trackHeight - 2.0f);

        if (r.getWidth() < audio_handles::minWidth)
            return {};

        return audio_handles::place (*clip, r.withTrimmedTop (clipHeaderHeight),
                                     view.beatToX (d.startBeat + clips::getFadeInBeats (*clip)),
                                     view.beatToX (d.endBeat - clips::getFadeOutBeats (*clip)));
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
            if (d.folded)
                continue;

            const auto top = view.trackToY (d.trackIndex);
            const auto bounds = juce::Rectangle<float>::leftTopRightBottom (view.beatToX (d.startBeat), top,
                                                                            view.beatToX (d.endBeat), top + view.trackHeight);

            if (bounds.intersects (area))
                result.addIfNotAlreadyThere (d.clip);
        }

        model.setSelection (result);
    }
}
