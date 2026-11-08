#pragma once

#include "ui/pianoroll/PianoRollModel.h"
#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    /** Mouse behaviour of the note grid (FL-style tools), separate from drawing so it can be
        tested with synthetic pointer events. Positions are in grid pixels.

        Draw / Paint: click empty space to add a note (Paint: drag to add a row of notes),
        drag a note to move it, drag its right edge to change its length, Shift+drag copies,
        Shift+drag on empty space (Draw) sets the new note's length, right button deletes.
        Ctrl+drag selects with a rectangle in any tool (Ctrl+Shift adds). Alt turns snap off.
        Delete / Mute: click or drag over notes. Slice: drag a line across notes to cut them.
        Select: drag a rectangle or move the selection. Middle button scrolls. */
    class GridGestures
    {
    public:
        struct Pointer
        {
            juce::Point<float> position;
            juce::ModifierKeys mods;
            int clicks = 1;
        };

        GridGestures (PianoRollModel&, View&);

        void down (const Pointer&);
        void drag (const Pointer&);
        void up (const Pointer&);

        bool isActive() const noexcept                  { return kind != Kind::none; }

        juce::MouseCursor getCursorFor (const Pointer&);

        /** What to draw on top of the notes while a gesture runs. */
        std::optional<juce::Rectangle<float>> getSelectionRectangle() const;
        std::optional<juce::Line<float>> getSliceLine() const;

        /** Double click on a note (opens the note's properties). */
        std::function<void (te::MidiNote&)> onNoteDoubleClicked;

        /** The view was scrolled by the middle button. */
        std::function<void()> onViewScrolled;

    private:
        enum class Kind { none, move, resize, stretchNew, paint, erase, mute, slice, rectangle, pan };

        PianoRollModel& model;
        View& view;

        Kind kind = Kind::none;
        Pointer downPointer;
        juce::Point<float> lastPosition;
        bool dragged = false;

        // move / resize
        juce::Array<te::MidiNote*> targets;
        std::vector<core::Note> originals;
        int grabbedIndex = 0;
        bool copyOnDrag = false, copied = false, selectOnlyOnClick = false;
        int auditionedPitch = -1;

        // paint
        double paintOrigin = 0.0, paintLength = 0.25;
        int lastPaintCell = 0;
        std::set<int> paintedCells;

        // delete / mute
        std::unordered_set<const te::MidiNote*> touched;

        // rectangle selection
        te::MidiNote* clickedNote = nullptr;
        juce::Array<te::MidiNote*> selectionAtStart;

        bool isSnapOff (const Pointer& p) const         { return p.mods.isAltDown(); }
        core::snap::Context getSnapContext() const;
        core::snap::Mode getSnapMode (const Pointer&) const;

        const DrawnNote* noteAt (juce::Point<float>);
        bool isOnRightEdge (const DrawnNote&, juce::Point<float>) const;

        void beginMoveOrResize (te::MidiNote& grabbed, Kind, bool copy);
        void dragMove (const Pointer&);
        void dragResize (const Pointer&);
        void addNoteAndMove (const Pointer&);
        void beginStretchNew (const Pointer&);
        void dragStretchNew (const Pointer&);
        void beginPaint (const Pointer&);
        void dragPaint (const Pointer&);
        void paintCell (int cell, int pitch);
        void touchAlong (juce::Point<float> from, juce::Point<float> to);
        void updateRectangleSelection (const Pointer&);
        void finishRectangle (const Pointer&);
        void performSlice (const Pointer&);
        void audition (int pitch, int velocity);
    };
}
