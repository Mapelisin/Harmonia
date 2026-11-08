#include "ui/pianoroll/GridGestures.h"

namespace harmonia::ui::pianoroll
{
    namespace snap = core::snap;
    namespace note_ops = core::note_ops;

    namespace
    {
        constexpr float dragThresholdPixels = 3.0f;
        constexpr float maxEdgeZonePixels = 7.0f;
        constexpr float sampleSpacingPixels = 2.0f;
        constexpr int newNoteVelocity = 100;
        constexpr double epsilon = 1.0e-9;
    }

    GridGestures::GridGestures (PianoRollModel& m, View& v)
        : model (m), view (v)
    {
    }

    snap::Context GridGestures::getSnapContext() const
    {
        model.setPixelsPerBeat (view.pixelsPerBeat);
        return model.getSnapContext();
    }

    snap::Mode GridGestures::getSnapMode (const Pointer& p) const
    {
        return isSnapOff (p) ? snap::Mode::none : model.getSnap();
    }

    const DrawnNote* GridGestures::noteAt (juce::Point<float> position)
    {
        return model.findNoteAt (view.xToBeat (position.x), view.yToPitch (position.y));
    }

    bool GridGestures::isOnRightEdge (const DrawnNote& d, juce::Point<float> position) const
    {
        const auto bounds = view.getNoteBounds (d.values.startBeat, d.values.lengthBeats, d.values.pitch);
        const auto zone = std::min (maxEdgeZonePixels, bounds.getWidth() * 0.35f);
        return position.x >= bounds.getRight() - zone;
    }

    //==============================================================================
    void GridGestures::down (const Pointer& p)
    {
        kind = Kind::none;
        downPointer = p;
        lastPosition = p.position;
        dragged = false;
        copied = false;
        selectOnlyOnClick = false;
        clickedNote = nullptr;
        touched.clear();

        if (p.mods.isMiddleButtonDown())
        {
            kind = Kind::pan;
            return;
        }

        const auto tool = model.getTool();
        const auto* hit = noteAt (p.position);

        if (p.mods.isRightButtonDown())
        {
            kind = Kind::erase;
            model.beginGesture ("Delete notes");
            touchAlong (p.position, p.position);
            return;
        }

        if (! p.mods.isLeftButtonDown())
            return;

        if (p.clicks >= 2 && hit != nullptr && (tool == Tool::draw || tool == Tool::paint || tool == Tool::select))
        {
            if (onNoteDoubleClicked != nullptr)
                onNoteDoubleClicked (*hit->note);

            return;
        }

        // Ctrl+click / Ctrl+drag selects in every tool.
        if (p.mods.isCtrlDown() && tool != Tool::select)
        {
            kind = Kind::rectangle;
            clickedNote = hit != nullptr ? hit->note : nullptr;
            selectionAtStart = model.getSelectedNotes();
            return;
        }

        switch (tool)
        {
            case Tool::draw:
            case Tool::paint:
                if (hit != nullptr)
                {
                    model.setNewNoteLength (hit->values.lengthBeats);
                    beginMoveOrResize (*hit->note, isOnRightEdge (*hit, p.position) ? Kind::resize : Kind::move,
                                       p.mods.isShiftDown());
                }
                else if (tool == Tool::paint)
                {
                    beginPaint (p);
                }
                else if (p.mods.isShiftDown())
                {
                    beginStretchNew (p);
                }
                else
                {
                    addNoteAndMove (p);
                }
                break;

            case Tool::select:
                if (hit != nullptr && ! p.mods.isShiftDown())
                {
                    auto& note = *hit->note;

                    if (model.isSelected (note))
                        selectOnlyOnClick = true;
                    else
                        model.setSelection ({ &note });

                    beginMoveOrResize (note, isOnRightEdge (*hit, p.position) ? Kind::resize : Kind::move, false);
                }
                else
                {
                    kind = Kind::rectangle;
                    clickedNote = hit != nullptr ? hit->note : nullptr;
                    selectionAtStart = model.getSelectedNotes();
                }
                break;

            case Tool::erase:
                kind = Kind::erase;
                model.beginGesture ("Delete notes");
                touchAlong (p.position, p.position);
                break;

            case Tool::mute:
                kind = Kind::mute;
                model.beginGesture ("Mute notes");
                touchAlong (p.position, p.position);
                break;

            case Tool::slice:
                kind = Kind::slice;
                break;
        }
    }

    void GridGestures::drag (const Pointer& p)
    {
        if (! dragged && p.position.getDistanceFrom (downPointer.position) < dragThresholdPixels)
            return;

        dragged = true;

        switch (kind)
        {
            case Kind::move:        dragMove (p); break;
            case Kind::resize:      dragResize (p); break;
            case Kind::stretchNew:  dragStretchNew (p); break;
            case Kind::paint:       dragPaint (p); break;
            case Kind::erase:
            case Kind::mute:        touchAlong (lastPosition, p.position); break;
            case Kind::rectangle:   updateRectangleSelection (p); break;

            case Kind::pan:
                view.scrollByPixels (lastPosition.x - p.position.x, lastPosition.y - p.position.y);

                if (onViewScrolled != nullptr)
                    onViewScrolled();
                break;

            case Kind::slice:
            case Kind::none:
                break;
        }

        lastPosition = p.position;
    }

    void GridGestures::up (const Pointer& p)
    {
        switch (kind)
        {
            case Kind::slice:
                performSlice (p);
                break;

            case Kind::rectangle:
                finishRectangle (p);
                break;

            case Kind::move:
            case Kind::resize:
                if (! dragged && selectOnlyOnClick && ! targets.isEmpty())
                    model.setSelection ({ targets[grabbedIndex] });

                if (kind == Kind::resize && dragged && ! targets.isEmpty())
                    model.setNewNoteLength (model.read (*targets[grabbedIndex]).lengthBeats);
                break;

            case Kind::stretchNew:
                if (! targets.isEmpty())
                    model.setNewNoteLength (model.read (*targets.getFirst()).lengthBeats);
                break;

            case Kind::paint:
            case Kind::erase:
            case Kind::mute:
            case Kind::pan:
            case Kind::none:
                break;
        }

        model.auditionStop();
        auditionedPitch = -1;
        kind = Kind::none;
        targets.clear();
        originals.clear();
        selectionAtStart.clear();
        clickedNote = nullptr;
        touched.clear();
        paintedCells.clear();
    }

    //==============================================================================
    void GridGestures::beginMoveOrResize (te::MidiNote& grabbed, Kind k, bool copy)
    {
        kind = k;
        copyOnDrag = copy && k == Kind::move;
        targets = model.isSelected (grabbed) ? model.getSelectedNotes() : juce::Array<te::MidiNote*> { &grabbed };
        grabbedIndex = std::max (0, targets.indexOf (&grabbed));

        originals.clear();

        for (auto* n : targets)
            originals.push_back (model.read (*n));

        model.beginGesture (k == Kind::resize ? "Change note length" : (copyOnDrag ? "Copy notes" : "Move notes"));

        const auto& grabbedValues = originals[(size_t) grabbedIndex];
        audition (grabbedValues.pitch, grabbedValues.velocity);
    }

    void GridGestures::dragMove (const Pointer& p)
    {
        if (targets.isEmpty())
            return;

        if (copyOnDrag && ! copied)
        {
            // The copies stay where the notes were; the originals (still selected) move.
            model.addNotes (originals);
            copied = true;
        }

        const auto mode = getSnapMode (p);
        const auto context = getSnapContext();
        const auto& grabbed = originals[(size_t) grabbedIndex];

        // The grabbed note's start lands on the grid; the others keep their distance to it.
        // While the mouse stays within the note's own grid cell the time doesn't change, so
        // moving a note up or down never quantizes it by accident.
        const auto rawDelta = view.xToBeat (p.position.x) - view.xToBeat (downPointer.position.x);
        const auto target = snap::snapNearest (grabbed.startBeat + rawDelta, mode, context);
        auto delta = std::abs (target - snap::snapNearest (grabbed.startBeat, mode, context)) < epsilon
                         ? 0.0 : target - grabbed.startBeat;
        delta = note_ops::limitMove (originals, delta, model.getMinBeat());

        const auto pitchDelta = note_ops::limitTransposition (
            originals, view.yToPitchUnclamped (p.position.y) - view.yToPitchUnclamped (downPointer.position.y));

        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
        {
            auto v = originals[(size_t) i];
            v.startBeat = snap::roundToTicks (v.startBeat + delta);
            v.pitch += pitchDelta;
            changes.push_back ({ targets[i], v });
        }

        model.updateNotes (changes);
        audition (grabbed.pitch + pitchDelta, grabbed.velocity);
    }

    void GridGestures::dragResize (const Pointer& p)
    {
        if (targets.isEmpty())
            return;

        const auto mode = getSnapMode (p);
        const auto context = getSnapContext();
        const auto& grabbed = originals[(size_t) grabbedIndex];

        const auto rawDelta = view.xToBeat (p.position.x) - view.xToBeat (downPointer.position.x);
        const auto targetEnd = snap::snapNearest (grabbed.getEndBeat() + rawDelta, mode, context);
        const auto lengthDelta = std::abs (targetEnd - snap::snapNearest (grabbed.getEndBeat(), mode, context)) < epsilon
                                     ? 0.0 : targetEnd - grabbed.getEndBeat();

        const auto grid = snap::getGridBeats (mode, context);
        const auto minLength = grid > 0.0 ? grid : 1.0 / snap::ticksPerBeat;

        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
        {
            auto v = originals[(size_t) i];
            v.lengthBeats = snap::roundToTicks (std::max (std::min (minLength, v.lengthBeats), v.lengthBeats + lengthDelta));
            changes.push_back ({ targets[i], v });
        }

        model.updateNotes (changes);
    }

    void GridGestures::addNoteAndMove (const Pointer& p)
    {
        core::Note n;
        n.pitch = view.yToPitch (p.position.y);
        n.startBeat = std::max (model.getMinBeat(),
                                snap::snapFloor (view.xToBeat (p.position.x), getSnapMode (p), getSnapContext()));
        n.lengthBeats = model.getNewNoteLength();
        n.velocity = newNoteVelocity;

        model.beginGesture ("Add note");
        auto* added = model.addNote (n);

        if (added == nullptr)
            return;

        // Keep dragging the new note, in the same undo step.
        kind = Kind::move;
        copyOnDrag = false;
        targets = { added };
        grabbedIndex = 0;
        originals = { model.read (*added) };
        audition (n.pitch, n.velocity);
    }

    void GridGestures::beginStretchNew (const Pointer& p)
    {
        const auto mode = getSnapMode (p);
        const auto context = getSnapContext();

        core::Note n;
        n.pitch = view.yToPitch (p.position.y);
        n.startBeat = std::max (model.getMinBeat(), snap::snapFloor (view.xToBeat (p.position.x), mode, context));
        n.lengthBeats = snap::getStepBeats (mode, context);
        n.velocity = newNoteVelocity;

        model.beginGesture ("Add note");

        if (auto* added = model.addNote (n))
        {
            kind = Kind::stretchNew;
            targets = { added };
            originals = { model.read (*added) };
            audition (n.pitch, n.velocity);
        }
    }

    void GridGestures::dragStretchNew (const Pointer& p)
    {
        if (targets.isEmpty())
            return;

        const auto mode = getSnapMode (p);
        const auto context = getSnapContext();
        const auto minLength = snap::getStepBeats (mode, context);

        auto v = originals.front();
        const auto end = snap::snapNearest (view.xToBeat (p.position.x), mode, context);
        v.lengthBeats = snap::roundToTicks (std::max (minLength, end - v.startBeat));
        model.updateNotes ({ { targets.getFirst(), v } });
    }

    void GridGestures::beginPaint (const Pointer& p)
    {
        kind = Kind::paint;
        paintLength = model.getNewNoteLength();
        paintOrigin = std::max (model.getMinBeat(),
                                snap::snapFloor (view.xToBeat (p.position.x), getSnapMode (p), getSnapContext()));
        lastPaintCell = 0;
        paintedCells.clear();

        model.beginGesture ("Paint notes");
        paintCell (0, view.yToPitch (p.position.y));
    }

    void GridGestures::dragPaint (const Pointer& p)
    {
        const auto cell = (int) std::floor ((view.xToBeat (p.position.x) - paintOrigin) / paintLength);
        const auto pitch = view.yToPitch (p.position.y);
        const auto step = cell >= lastPaintCell ? 1 : -1;

        // Fill every cell passed since the last event, so fast strokes leave no gaps.
        for (int c = lastPaintCell; c != cell + step; c += step)
            paintCell (c, pitch);

        lastPaintCell = cell;
    }

    void GridGestures::paintCell (int cell, int pitch)
    {
        if (paintedCells.count (cell) != 0)
            return;

        const auto start = snap::roundToTicks (paintOrigin + cell * paintLength);

        if (start < model.getMinBeat() - epsilon)
            return;

        paintedCells.insert (cell);

        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = paintLength;
        n.velocity = newNoteVelocity;
        model.addNote (n);
        audition (pitch, n.velocity);
    }

    void GridGestures::touchAlong (juce::Point<float> from, juce::Point<float> to)
    {
        const auto distance = from.getDistanceFrom (to);
        const auto steps = std::max (1, (int) std::ceil (distance / sampleSpacingPixels));

        juce::Array<te::MidiNote*> hits;

        for (int i = 0; i <= steps; ++i)
        {
            const auto point = from + (to - from) * ((float) i / (float) steps);

            if (auto* d = noteAt (point))
                if (touched.insert (d->note).second)
                    hits.add (d->note);
        }

        if (hits.isEmpty())
            return;

        if (kind == Kind::erase)
        {
            model.removeNotes (hits);
            return;
        }

        std::vector<core::NoteEditor::Change> changes;

        for (auto* n : hits)
        {
            auto v = model.read (*n);
            v.muted = ! v.muted;
            changes.push_back ({ n, v });
        }

        model.updateNotes (changes);
    }

    void GridGestures::updateRectangleSelection (const Pointer& p)
    {
        const auto area = juce::Rectangle<float> (downPointer.position, p.position);
        const auto startBeat = view.xToBeat (area.getX());
        const auto endBeat = view.xToBeat (area.getRight());
        const auto topPitch = view.yToPitch (area.getY());
        const auto bottomPitch = view.yToPitch (area.getBottom());

        juce::Array<te::MidiNote*> inside;
        const auto& notes = model.getNotes();
        const auto [first, last] = model.getNotesInRange (startBeat, endBeat);

        for (auto i = first; i < last; ++i)
        {
            const auto& v = notes[i].values;

            if (v.pitch >= bottomPitch && v.pitch <= topPitch
                && v.startBeat < endBeat && v.getEndBeat() > startBeat)
                inside.add (notes[i].note);
        }

        if (p.mods.isShiftDown())
        {
            model.setSelection (selectionAtStart);
            model.addToSelection (inside);
        }
        else
        {
            model.setSelection (inside);
        }
    }

    void GridGestures::finishRectangle (const Pointer& p)
    {
        if (dragged)
            return;

        // A click without dragging.
        if (clickedNote != nullptr)
        {
            if (p.mods.isShiftDown())
                model.setSelected (*clickedNote, ! model.isSelected (*clickedNote));
            else
                model.setSelection ({ clickedNote });

            const auto v = model.read (*clickedNote);
            audition (v.pitch, v.velocity);
        }
        else if (! p.mods.isShiftDown())
        {
            model.deselectAll();
        }
    }

    void GridGestures::performSlice (const Pointer& p)
    {
        const auto mode = getSnapMode (p);
        const auto context = getSnapContext();
        const auto a = downPointer.position;
        const auto b = p.position;

        juce::Array<te::MidiNote*> cutNotes;
        std::vector<double> cutBeats;

        if (a.getDistanceFrom (b) < dragThresholdPixels)
        {
            // A click cuts the note under the mouse.
            if (auto* d = noteAt (a))
            {
                cutNotes.add (d->note);
                cutBeats.push_back (snap::snapNearest (view.xToBeat (a.x), mode, context));
            }
        }
        else
        {
            const auto [first, last] = model.getNotesInRange (view.xToBeat (std::min (a.x, b.x)),
                                                              view.xToBeat (std::max (a.x, b.x)) + epsilon);
            const auto& notes = model.getNotes();

            for (auto i = first; i < last; ++i)
            {
                const auto& v = notes[i].values;
                const auto rowTop = view.pitchToY (v.pitch);
                const auto rowCentre = rowTop + view.rowHeight * 0.5f;
                float x = 0.0f;

                if (std::abs (b.y - a.y) < 1.0f)
                {
                    // A (nearly) horizontal line cuts only the row it is drawn in.
                    if (a.y < rowTop || a.y >= rowTop + view.rowHeight)
                        continue;

                    x = a.x;
                }
                else
                {
                    if (rowCentre < std::min (a.y, b.y) || rowCentre > std::max (a.y, b.y))
                        continue;

                    x = a.x + (b.x - a.x) * (rowCentre - a.y) / (b.y - a.y);
                }

                cutNotes.add (notes[i].note);
                cutBeats.push_back (snap::snapNearest (view.xToBeat (x), mode, context));
            }
        }

        std::vector<core::NoteEditor::Change> shortened;
        std::vector<core::Note> rightParts;

        for (int i = 0; i < cutNotes.size(); ++i)
        {
            const auto v = model.read (*cutNotes[i]);
            const auto beat = cutBeats[(size_t) i];

            if (beat <= v.startBeat + epsilon || beat >= v.getEndBeat() - epsilon)
                continue;

            const auto [left, right] = note_ops::split (v, beat);
            shortened.push_back ({ cutNotes[i], left });
            rightParts.push_back (right);
        }

        if (shortened.empty())
            return;

        model.beginGesture ("Slice notes");
        model.updateNotes (shortened);
        model.addNotes (rightParts);
    }

    void GridGestures::audition (int pitch, int velocity)
    {
        if (pitch == auditionedPitch)
            return;

        auditionedPitch = pitch;
        model.auditionStart (pitch, velocity);
    }

    //==============================================================================
    juce::MouseCursor GridGestures::getCursorFor (const Pointer& p)
    {
        switch (model.getTool())
        {
            case Tool::draw:
            case Tool::paint:
            case Tool::select:
                if (auto* d = noteAt (p.position))
                    return isOnRightEdge (*d, p.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                          : juce::MouseCursor::DraggingHandCursor;

                return juce::MouseCursor::NormalCursor;

            case Tool::erase:
            case Tool::mute:
                return juce::MouseCursor::PointingHandCursor;

            case Tool::slice:
                return juce::MouseCursor::IBeamCursor;
        }

        return juce::MouseCursor::NormalCursor;
    }

    std::optional<juce::Rectangle<float>> GridGestures::getSelectionRectangle() const
    {
        if (kind == Kind::rectangle && dragged)
            return juce::Rectangle<float> (downPointer.position, lastPosition);

        return std::nullopt;
    }

    std::optional<juce::Line<float>> GridGestures::getSliceLine() const
    {
        if (kind == Kind::slice)
            return juce::Line<float> (downPointer.position, lastPosition);

        return std::nullopt;
    }
}
