#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui::arrangement
{
    /** Zoom and scroll of the arrangement: song beats across, tracks down (one row per track).
        Shared by the ruler, the track headers and the clip area so they always line up. */
    struct ArrangementView
    {
        static constexpr double minPixelsPerBeat = 1.0, maxPixelsPerBeat = 240.0;
        static constexpr float trackHeight = 56.0f;

        double pixelsPerBeat = 16.0;    // 64 pixels per 4/4 bar
        double leftBeat = 0.0;          // song beat at x = 0
        float scrollY = 0.0f;           // pixels of the track list scrolled off the top

        float width = 0.0f, height = 0.0f;  // size of the clip area in pixels
        double contentBeats = 64.0;         // how far the horizontal scroll range goes
        int numTracks = 0;

        float beatToX (double beat) const noexcept      { return (float) ((beat - leftBeat) * pixelsPerBeat); }
        double xToBeat (float x) const noexcept         { return leftBeat + (double) x / pixelsPerBeat; }
        double getRightBeat() const noexcept            { return xToBeat (width); }

        float trackToY (int index) const noexcept       { return (float) index * trackHeight - scrollY; }

        /** Row under y; may be below the last track (>= numTracks) or negative. */
        int yToTrack (float y) const noexcept           { return (int) std::floor ((y + scrollY) / trackHeight); }

        float getContentHeight() const noexcept         { return (float) numTracks * trackHeight; }

        /** Zooms keeping the beat under `anchorX` in place. */
        void zoomHorizontally (double factor, float anchorX)
        {
            const auto anchorBeat = xToBeat (anchorX);
            pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat * factor);
            leftBeat = anchorBeat - (double) anchorX / pixelsPerBeat;
            clamp();
        }

        void scrollByPixels (double dx, double dy)
        {
            leftBeat += dx / pixelsPerBeat;
            scrollY += (float) dy;
            clamp();
        }

        /** Keeps scroll positions within the content. */
        void clamp()
        {
            const auto visibleBeats = (double) width / pixelsPerBeat;
            leftBeat = juce::jlimit (0.0, std::max (0.0, contentBeats - visibleBeats), leftBeat);
            scrollY = juce::jlimit (0.0f, std::max (0.0f, getContentHeight() + trackHeight - height), scrollY);
        }
    };
}
