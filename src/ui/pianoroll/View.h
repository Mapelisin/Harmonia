#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui::pianoroll
{
    /** Zoom and scroll of the piano roll: converts between pixels and (song beat, pitch).
        Shared by the note grid, keyboard, ruler and lower panel so they always line up. */
    struct View
    {
        static constexpr int numPitches = 128;
        static constexpr double minPixelsPerBeat = 4.0, maxPixelsPerBeat = 1600.0;
        static constexpr float minRowHeight = 6.0f, maxRowHeight = 40.0f;

        double pixelsPerBeat = 80.0;    // horizontal zoom
        float rowHeight = 14.0f;        // vertical zoom: pixels per semitone
        double leftBeat = 0.0;          // song beat at x = 0
        float topRow = 40.0f;           // row at y = 0; row 0 is pitch 127

        float width = 0.0f, height = 0.0f;   // size of the note area in pixels
        double contentBeats = 64.0;          // how far the horizontal scroll range goes

        float beatToX (double beat) const noexcept      { return (float) ((beat - leftBeat) * pixelsPerBeat); }
        double xToBeat (float x) const noexcept         { return leftBeat + (double) x / pixelsPerBeat; }

        /** Top edge of a pitch's row. */
        float pitchToY (int pitch) const noexcept       { return ((float) (numPitches - 1 - pitch) - topRow) * rowHeight; }

        /** Pitch of the row at y (unclamped, so drags above or below the range can be clamped by the caller). */
        int yToPitchUnclamped (float y) const noexcept  { return numPitches - 1 - (int) std::floor (y / rowHeight + topRow); }
        int yToPitch (float y) const noexcept           { return juce::jlimit (0, numPitches - 1, yToPitchUnclamped (y)); }

        juce::Rectangle<float> getNoteBounds (double startBeat, double lengthBeats, int pitch) const noexcept;

        double getRightBeat() const noexcept            { return xToBeat (width); }
        float getVisibleRows() const noexcept           { return height / rowHeight; }

        /** Zooms keeping the beat under `anchorX` in place. */
        void zoomHorizontally (double factor, float anchorX);

        /** Zooms keeping the pitch under `anchorY` in place. */
        void zoomVertically (float factor, float anchorY);

        void scrollByPixels (double dx, double dy);

        /** Puts `pitch` in the middle of the visible rows. */
        void centreOnPitch (int pitch);

        /** Keeps scroll positions within the content. */
        void clamp();
    };
}
