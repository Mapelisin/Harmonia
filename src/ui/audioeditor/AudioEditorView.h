#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui::audioeditor
{
    /** Zoom and scroll of the audio clip editor: pixels <-> seconds of the clip's sound file
        (0 is where the file begins). Shared by the waveform and its ruler so they line up. */
    struct View
    {
        static constexpr double minPixelsPerSecond = 1.0, maxPixelsPerSecond = 20000.0;

        double pixelsPerSecond = 100.0;     // zoom
        double leftSeconds = 0.0;           // file time at x = 0
        float width = 0.0f;                 // of the waveform, in pixels
        double fileSeconds = 0.0;           // how long the file is: the scroll range

        float secondsToX (double seconds) const noexcept    { return (float) ((seconds - leftSeconds) * pixelsPerSecond); }
        double xToSeconds (float x) const noexcept          { return leftSeconds + (double) x / pixelsPerSecond; }
        double getRightSeconds() const noexcept             { return xToSeconds (width); }

        /** Zooms keeping the time under `anchorX` in place. */
        void zoom (double factor, float anchorX);

        void scrollByPixels (double dx);

        /** Shows `range` (file seconds) with a little room either side. */
        void show (juce::Range<double> range);

        /** Keeps the zoom and the scroll within the file (and a little either side of it); a file
            much shorter than the view sits in its middle. */
        void clamp();
    };
}
