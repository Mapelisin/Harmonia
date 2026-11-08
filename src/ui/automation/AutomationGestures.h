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

        void down (const Pointer&);
        void drag (const Pointer&);
        void up (const Pointer&);

        /** The point being dragged, or -1. */
        int getDraggedPoint() const noexcept                        { return dragged; }

        juce::MouseCursor getCursorFor (juce::Point<float>) const;

        static constexpr float margin = 10.0f, hitRadius = 6.0f;

    private:
        arrangement::ArrangementModel& model;
        const arrangement::ArrangementView& view;
        te::AutomatableParameter::Ptr parameter;
        float height = 100.0f;
        int dragged = -1;
        bool erasing = false;
        double grabBeat = 0.0;

        double toBeat (const Pointer&) const;
        void eraseAt (juce::Point<float>);
    };
}
