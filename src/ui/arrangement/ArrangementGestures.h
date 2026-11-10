#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"
#include "ui/AudioClipHandles.h"

namespace harmonia::ui::arrangement
{
    /** Mouse behaviour of the clip area, separate from drawing so it can be tested with
        synthetic pointer events. Positions are in clip-area pixels.

        Click a clip: select it and open it in the piano roll. Drag it by its name strip (the
        top of it): move (also to another track; the whole selection moves). Shift+drag: linked
        copy (as FL clones a pattern); Ctrl+drag: independent copy; Ctrl+click: add to / remove
        from the selection. Drag a clip's left or right edge: show or hide its beginning or end.

        Drag anywhere else in the rows, over clips (below their name strips) or empty space:
        select a piece of time on the rows the drag crosses, whatever the clips' borders, as in
        Ableton's arrangement (ArrangementModel::TimeSelection; the edit keys then act on it).
        Drag the selected piece: move what is in it, Ctrl: copy it (applied when the button is
        let go; getTimeMoveTarget is where it would land). A click lets the piece go.

        Right button: delete (drag over clips to delete several). Shift or Ctrl + drag on empty
        space: select whole clips with a rectangle. Double-click on empty space: new clip (not on
        audio tracks; audio clips only move to audio tracks, MIDI clips to instrument tracks).
        The small mark at a clip's top left opens its menu. Alt turns snap off. Middle button
        scrolls.

        An audio clip has three handles at the top of its waveform: drag the left / right one
        across for a fade in / out, the middle one up and down for the clip's gain; double-click
        one to reset it. */
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

        /** While the selected piece of time is being dragged: where it would land. */
        std::optional<ArrangementModel::TimeSelection> getTimeMoveTarget() const;

        /** The piece of time couldn't go where it was dropped (notes onto an audio track...). */
        std::function<void (const juce::String&)> onProblem;

        /** The clip's menu mark was clicked (position in clip-area pixels). */
        std::function<void (te::Clip&, juce::Point<float>)> onClipMenuRequested;

        /** The view was scrolled by the middle button. */
        std::function<void()> onViewScrolled;

        /** Size of the menu mark at a clip's top left, and of the grab zones at its edges. */
        static constexpr float menuMarkSize = 14.0f, edgeGrabPixels = 6.0f;

        /** A clip's header (name, menu mark) is as tall as the menu mark; its body is below. */
        static constexpr float clipHeaderHeight = menuMarkSize;

        /** Where an audio clip's handles are drawn (clip-area pixels; the body is the waveform below
            the header); none for other clips and clips too narrow for them. */
        static std::optional<audio_handles::Handles> getAudioHandles (const DrawnClip&, const ArrangementView&);

        /** While a fade or the gain is dragged: what to show by the handle ("Fade in 1.5 beats"). */
        std::optional<std::pair<juce::String, juce::Point<float>>> getReadout();

        enum class Zone { body, startEdge, endEdge, menu, fadeIn, fadeOut, gain };

        /** Clip and zone under a position, if any. */
        std::optional<std::pair<DrawnClip, Zone>> hitTest (juce::Point<float>);

    private:
        enum class Kind { none, move, resizeStart, resizeEnd, erase, rectangle, pan, fadeIn, fadeOut, gain, timeSelect, moveTime };

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

        // a piece of time: being selected (a click instead: on this clip, if any), or being moved
        te::Clip::Ptr clickedClip;
        std::optional<ArrangementModel::TimeSelection> movingPiece;
        double moveBeats = 0.0;
        int moveRows = 0;

        void beginTime (const Pointer&, te::Clip* clipUnderIt);
        void dragTime (const Pointer&);
        void dragPiece (const Pointer&);

        core::snap::Mode getSnapMode (const Pointer&) const;
        int trackAt (float y) const;

        void beginMove (te::Clip&, const Pointer&);
        void dragMove (const Pointer&);
        void makeCopies();
        void dragResize (const Pointer&);
        void dragHandle (const Pointer&);
        float grabbedGainY = 0.0f;
        void eraseAlong (juce::Point<float> from, juce::Point<float> to);
        void updateRectangleSelection (const Pointer&);
    };
}
