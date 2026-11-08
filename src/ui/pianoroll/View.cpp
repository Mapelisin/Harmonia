#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    juce::Rectangle<float> View::getNoteBounds (double startBeat, double lengthBeats, int pitch) const noexcept
    {
        const auto x1 = beatToX (startBeat);
        const auto x2 = beatToX (startBeat + lengthBeats);
        return { x1, pitchToY (pitch), std::max (1.0f, x2 - x1), rowHeight };
    }

    void View::zoomHorizontally (double factor, float anchorX)
    {
        const auto anchorBeat = xToBeat (anchorX);
        pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat * factor);
        leftBeat = anchorBeat - (double) anchorX / pixelsPerBeat;
        clamp();
    }

    void View::zoomVertically (float factor, float anchorY)
    {
        const auto anchorRow = anchorY / rowHeight + topRow;
        rowHeight = juce::jlimit (minRowHeight, maxRowHeight, rowHeight * factor);
        topRow = anchorRow - anchorY / rowHeight;
        clamp();
    }

    void View::scrollByPixels (double dx, double dy)
    {
        leftBeat += dx / pixelsPerBeat;
        topRow += (float) dy / rowHeight;
        clamp();
    }

    void View::centreOnPitch (int pitch)
    {
        topRow = (float) (numPitches - 1 - pitch) + 0.5f - getVisibleRows() * 0.5f;
        clamp();
    }

    void View::clamp()
    {
        const auto visibleBeats = width > 0.0f ? (double) width / pixelsPerBeat : 0.0;
        leftBeat = juce::jlimit (0.0, std::max (0.0, contentBeats - visibleBeats), leftBeat);
        topRow = juce::jlimit (0.0f, std::max (0.0f, (float) numPitches - getVisibleRows()), topRow);
    }
}
