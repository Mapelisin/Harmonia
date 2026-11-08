#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::arrangement
{
    /** Mouse behaviour of the clip area, separate from drawing so it can be tested with
        synthetic pointer events. Positions are in clip-area pixels.

        Click a clip: select it and open it in the piano roll. Drag it: move (also to another
        track; the whole selection moves). Shift+drag: linked copy (as FL clones a pattern);
        Ctrl+drag: independent copy; Ctrl+click: add to / remove from the selection. Drag a
        clip's left or right edge: show or hide its beginning or end. Right button: delete
        (drag over clips to delete several). Drag on empty space: select with a rectangle
        (Shift or Ctrl adds). Double-click on empty space: new clip (not on audio tracks; audio
        clips only move to audio tracks, MIDI clips to instrument tracks). The small mark at a clip's
        top left opens its menu. Alt turns snap off. Middle button scrolls. */
    class ArrangementGestures
    {
    public:
        struct Pointer
        {
            juce::Point<float> position;
            juce::ModifierKeys mods;
            int clicks = 1;
        };

        ArrangementGestures (ArrangementModel&, ArrangementView&);

        void down (const Pointer&);
        void drag (const Pointer&);
        void up (const Pointer&);

        bool isActive() const noexcept                  { return kind != Kind::none; }

        juce::MouseCursor getCursorFor (const Pointer&);

        /** The selection rectangle while one is being dragged. */
        std::optional<juce::Rectangle<float>> getSelectionRectangle() const;

        /** The clip's menu mark was clicked (position in clip-area pixels). */
        std::function<void (te::Clip&, juce::Point<float>)> onClipMenuRequested;

        /** The view was scrolled by the middle button. */
        std::function<void()> onViewScrolled;

        /** Size of the menu mark at a clip's top left, and of the grab zones at its edges. */
        static constexpr float menuMarkSize = 14.0f, edgeGrabPixels = 6.0f;

        enum class Zone { body, startEdge, endEdge, menu };

        /** Clip and zone under a position, if any. */
        std::optional<std::pair<DrawnClip, Zone>> hitTest (juce::Point<float>);

    private:
        enum class Kind { none, move, resizeStart, resizeEnd, erase, rectangle, pan };

        ArrangementModel& model;
        ArrangementView& view;

        Kind kind = Kind::none;
        Pointer downPointer;
        juce::Point<float> lastPosition;
        bool dragged = false;

        // move / resize
        struct Original
        {
            te::Clip::Ptr clip;
            double startBeat = 0.0;
            int trackIndex = 0;
        };

        std::vector<Original> targets;
        te::Clip::Ptr grabbed;
        double grabbedStart = 0.0;
        enum class Copy { none, linked, independent } copy = Copy::none;
        bool toggleOnClick = false, selectOnlyOnClick = false;
        double appliedDelta = 0.0;
        int appliedTrackDelta = 0;

        // rectangle
        juce::Array<te::Clip*> selectionAtStart;

        core::snap::Mode getSnapMode (const Pointer&) const;
        int trackAt (float y) const;

        void beginMove (te::Clip&, const Pointer&);
        void dragMove (const Pointer&);
        void makeCopies();
        void dragResize (const Pointer&);
        void eraseAlong (juce::Point<float> from, juce::Point<float> to);
        void updateRectangleSelection (const Pointer&);
    };
}
