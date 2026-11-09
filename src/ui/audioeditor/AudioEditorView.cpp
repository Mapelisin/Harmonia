#include "ui/audioeditor/AudioEditorView.h"

namespace harmonia::ui::audioeditor
{
    namespace
    {
        /** How far past either end of the file the view may go: a part of its width. */
        constexpr double overscroll = 0.1;
    }

    void View::zoom (double factor, float anchorX)
    {
        const auto anchor = xToSeconds (anchorX);
        pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, pixelsPerSecond * factor);
        leftSeconds = anchor - (double) anchorX / pixelsPerSecond;
        clamp();
    }

    void View::scrollByPixels (double dx)
    {
        leftSeconds += dx / pixelsPerSecond;
        clamp();
    }

    void View::show (juce::Range<double> range)
    {
        if (width <= 0.0f || range.getLength() <= 0.0)
            return;

        const auto margin = range.getLength() * 0.06;
        pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, (double) width / (range.getLength() + 2.0 * margin));
        leftSeconds = range.getStart() - margin;
        clamp();
    }

    void View::clamp()
    {
        pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, pixelsPerSecond);

        const auto visible = (double) width / pixelsPerSecond;
        const auto room = std::max (visible, fileSeconds) * overscroll;

        // Zoomed out past the whole file: it sits in the middle.
        if (visible >= fileSeconds + 2.0 * room)
        {
            leftSeconds = (fileSeconds - visible) / 2.0;
            return;
        }

        leftSeconds = juce::jlimit (-room, fileSeconds + room - visible, leftSeconds);
    }
}
