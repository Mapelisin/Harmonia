#pragma once

#include "audio/Automation.h"
#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::automation
{
    /** Mouse editing of a parameter's automation line, as the lane shows it: across, song beats as
        in the arrangement (the same view); up and down, the value (top = 1, bottom = 0).

        - Click on empty space: a new point there (on the arrangement's grid, Alt: free), and keep
          dragging to place it.
        - Drag a point: move it (not past its neighbours; Shift: only up and down).
        - Right-click (or right-drag over points), or double-click a point: delete.
        - Drag the small handle in the middle of a stretch of the line, up or down: bend the
          stretch (the handle stays under the mouse). Double-click or right-click the handle:
          straight again.
        - Ctrl + drag: a rectangle selects the points in it (Ctrl+Shift: adds them to those
          selected). Ctrl + click a point: into the selection or out of it; Ctrl + click on empty
          space: nothing selected.
        - Drag one of several selected points: they all move, keeping their shape (not past the
          points that stay; Shift: only up and down).
        Every gesture that changes the line is one undo step, and so is each command on the
        selected points (delete, cut, paste). */
    class AutomationGestures
    {
    public:
        struct Pointer
        {
            juce::Point<float> position;
            juce::ModifierKeys mods;
            int clicks = 1;
        };

        AutomationGestures (arrangement::ArrangementModel&, const arrangement::ArrangementView&);

        void setParameter (te::AutomatableParameter*);
        te::AutomatableParameter* getParameter() const noexcept     { return parameter.get(); }

        /** Height of the lane in pixels. */
        void setHeight (float h) noexcept                           { height = h; }

        float valueToY (float value) const noexcept;
        float yToValue (float y) const noexcept;

        /** The point drawn under `position`, or -1. */
        int pointAt (juce::Point<float>) const;

        /** Where the bend handle of the stretch after point `index` is drawn. Nothing where the
            line can't bend (see audio::automation::getBendHandle) or the stretch is narrower than
            `minBendPixels` on screen. */
        std::optional<juce::Point<float>> getBendHandle (int index) const;

        /** The stretch (its first point) whose bend handle is under `position`, or -1. */
        int bendAt (juce::Point<float>) const;

        //==============================================================================
        /** The selected points, in time order. The selection is of the line as it is: when the
            line changes other than by these gestures and commands (undo, a recording...), it is
            gone. */
        std::vector<int> getSelection() const;
        bool isSelected (int index) const;
        void setSelection (std::vector<int> indices);
        void selectAll();
        void clearSelection()                                       { setSelection ({}); }

        /** The rectangle being drawn to select points, while one is. */
        std::optional<juce::Rectangle<float>> getRectangle() const;

        /** Commands on the selected points; false when there was nothing to do. */
        bool deleteSelected();
        bool copySelected();
        bool cutSelected();

        /** Whether points were copied (in any lane: the values go from the bottom of one
            parameter's range to the top of another's). */
        static bool canPaste();

        /** The copied points, the first of them at `beat`, in place of what the line had over
            their length; they become the selection. */
        bool paste (double beat);

        void down (const Pointer&);
        void drag (const Pointer&);
        void up (const Pointer&);

        /** The point being dragged, or -1. */
        int getDraggedPoint() const noexcept                        { return dragged; }

        /** The stretch being bent (its first point), or -1. */
        int getBentStretch() const noexcept                         { return bending; }

        juce::MouseCursor getCursorFor (juce::Point<float>) const;

        static constexpr float margin = 10.0f, hitRadius = 6.0f, bendHitRadius = 5.0f, minBendPixels = 24.0f;

    private:
        arrangement::ArrangementModel& model;
        const arrangement::ArrangementView& view;
        te::AutomatableParameter::Ptr parameter;
        float height = 100.0f;
        int dragged = -1, bending = -1;
        bool erasing = false;
        double grabBeat = 0.0;
        float bendGrabOffset = 0.0f;        // from the mouse to the handle, when it was taken

        std::vector<int> selection;                             // in time order
        std::vector<audio::automation::Point> selectedLine;     // the line they were selected on
        bool selecting = false, movingSelection = false;
        juce::Point<float> rectangleStart, rectangleEnd;
        std::vector<int> selectionAtStart;                      // what a rectangle adds to
        std::vector<audio::automation::Point> moveFrom;         // the line when a move of several began
        float grabValue = 0.0f;                                 // the value under the mouse then

        double toBeat (const Pointer&) const;
        void eraseAt (juce::Point<float>);
        void selectInRectangle();
    };
}
