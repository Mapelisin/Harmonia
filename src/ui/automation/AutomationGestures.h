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
        Every gesture is one undo step. */
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

        double toBeat (const Pointer&) const;
        void eraseAt (juce::Point<float>);
    };
}
