#include "PianoRollView.h"

namespace harmonia::spike
{
    namespace
    {
        const juce::Colour background   { 0xff22252a };
        const juce::Colour blackKeyRow  { 0xff1b1d21 };
        const juce::Colour stepLine     { 0xff2a2d33 };
        const juce::Colour beatLine     { 0xff353941 };
        const juce::Colour barLine      { 0xff4a505a };
        const juce::Colour noteOutline  { 0xff0e1012 };
        const juce::Colour noteColours[] = { juce::Colour (0xff7fd18b), juce::Colour (0xff6fb4f0),
                                             juce::Colour (0xfff0b46f), juce::Colour (0xffe07fb0) };

        bool isBlackKey (int pitch) noexcept
        {
            const int k = pitch % 12;
            return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
        }

        double nowMs() noexcept { return juce::Time::getMillisecondCounterHiRes(); }
    }

    void PianoRollView::setNotes (const NoteSet* newNotes)
    {
        notes = newNotes;
        repaint();
    }

    void PianoRollView::setView (double newFirstBeat, double newPixelsPerBeat, double newTopPitch, float newRowHeight)
    {
        firstBeat = std::max (0.0, newFirstBeat);
        pixelsPerBeat = juce::jlimit (0.05, 2000.0, newPixelsPerBeat);
        topPitch = juce::jlimit (10.0, 131.0, newTopPitch);
        rowHeight = juce::jlimit (3.0f, 60.0f, newRowHeight);
        repaint();
    }

    void PianoRollView::paint (juce::Graphics& g)
    {
        const auto start = nowMs();
        paintStartTimesMs.push_back (start);

        const auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) keyboardWidth);

        g.fillAll (background);
        paintRows (g, area);
        paintGrid (g, area);
        paintNotes (g, area);
        paintKeyboard (g);

        paintDurationsMs.push_back (nowMs() - start);
    }

    void PianoRollView::paintRows (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        juce::RectangleList<float> blackRows;
        const int lowest = (int) std::floor (topPitch - getHeight() / rowHeight);

        for (int pitch = (int) std::ceil (topPitch); pitch >= std::max (0, lowest); --pitch)
            if (isBlackKey (pitch))
                blackRows.addWithoutMerging ({ area.getX(), pitchToY (pitch), area.getWidth(), rowHeight });

        g.setColour (blackKeyRow);
        g.fillRectList (blackRows);
    }

    void PianoRollView::paintGrid (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const double lastBeat = firstBeat + area.getWidth() / pixelsPerBeat;

        // Draw a level of grid lines only when its spacing is wide enough to be readable.
        auto addLines = [&] (double spacingBeats, juce::Colour colour)
        {
            if (spacingBeats * pixelsPerBeat < 6.0)
                return;

            juce::RectangleList<float> lines;

            for (double b = std::ceil (firstBeat / spacingBeats) * spacingBeats; b <= lastBeat; b += spacingBeats)
                lines.addWithoutMerging ({ beatToX (b), area.getY(), 1.0f, area.getHeight() });

            g.setColour (colour);
            g.fillRectList (lines);
        };

        addLines (0.25, stepLine);
        addLines (1.0, beatLine);

        // Bars: when zoomed far out, thin them to every 4th / 16th bar.
        double barSpacing = 4.0;
        while (barSpacing * pixelsPerBeat < 6.0)
            barSpacing *= 4.0;

        addLines (barSpacing, barLine);
    }

    void PianoRollView::paintNotes (juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (notes == nullptr)
            return;

        const double lastBeat = firstBeat + area.getWidth() / pixelsPerBeat;
        const auto [first, last] = notes->findOverlapping (firstBeat, lastBeat);
        const auto& all = notes->getNotes();

        constexpr int numColours = (int) std::size (noteColours);
        juce::RectangleList<float> outlines, bodies[numColours];
        std::vector<std::pair<juce::Rectangle<float>, int>> labels;
        size_t visible = 0;

        for (size_t i = first; i < last; ++i)
        {
            const auto& n = all[i];
            const float y = pitchToY (n.pitch);

            if (y + rowHeight < area.getY() || y > area.getBottom())
                continue;

            const float x = beatToX (n.startBeat);
            const float w = std::max (1.0f, (float) (n.lengthBeats * pixelsPerBeat));

            if (x + w < area.getX())
                continue;

            ++visible;
            const juce::Rectangle<float> r (x, y, w, rowHeight);
            outlines.addWithoutMerging (r);

            if (w > 3.0f && rowHeight > 3.0f)
                bodies[n.colour].addWithoutMerging (r.reduced (1.0f));

            if (showLabels && w > 26.0f && rowHeight >= 10.0f)
                labels.emplace_back (r.reduced (3.0f, 0.0f), n.pitch);
        }

        lastVisibleNoteCount = visible;

        g.saveState();
        g.reduceClipRegion (area.toNearestInt());

        g.setColour (noteOutline);
        g.fillRectList (outlines);

        for (int c = 0; c < numColours; ++c)
        {
            g.setColour (noteColours[c]);
            g.fillRectList (bodies[c]);
        }

        if (! labels.empty())
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            g.setColour (noteOutline);
            g.setFont (juce::FontOptions (std::min (rowHeight - 2.0f, 12.0f)));

            for (auto& [r, pitch] : labels)
                g.drawText (juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 1),
                            r, juce::Justification::centredLeft, false);
        }

        g.restoreState();
    }

    void PianoRollView::paintKeyboard (juce::Graphics& g) const
    {
        const float w = (float) keyboardWidth;
        g.setColour (juce::Colour (0xffe8e8e8));
        g.fillRect (0.0f, 0.0f, w, (float) getHeight());

        juce::RectangleList<float> blackKeys, separators;
        const int lowest = (int) std::floor (topPitch - getHeight() / rowHeight);

        for (int pitch = (int) std::ceil (topPitch); pitch >= std::max (0, lowest); --pitch)
        {
            const float y = pitchToY (pitch);

            if (isBlackKey (pitch))
                blackKeys.addWithoutMerging ({ 0.0f, y, w * 0.62f, rowHeight });
            else
                separators.addWithoutMerging ({ 0.0f, y + rowHeight - 1.0f, w, 1.0f });
        }

        g.setColour (juce::Colour (0xffb0b0b0));
        g.fillRectList (separators);
        g.setColour (juce::Colour (0xff181818));
        g.fillRectList (blackKeys);
    }

    void PianoRollView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (e.mods.isCtrlDown())
        {
            // Zoom horizontally around the mouse position.
            const double beatAtMouse = firstBeat + (e.position.x - keyboardWidth) / pixelsPerBeat;
            const double newPpb = pixelsPerBeat * std::pow (1.25, wheel.deltaY * 4.0);
            setView (beatAtMouse - (e.position.x - keyboardWidth) / newPpb, newPpb, topPitch, rowHeight);
        }
        else if (e.mods.isShiftDown())
        {
            setView (firstBeat - wheel.deltaY * 200.0 / pixelsPerBeat, pixelsPerBeat, topPitch, rowHeight);
        }
        else
        {
            setView (firstBeat, pixelsPerBeat, topPitch + wheel.deltaY * 12.0, rowHeight);
        }
    }

    void PianoRollView::mouseDown (const juce::MouseEvent&)
    {
        dragStartView = { firstBeat, topPitch };
    }

    void PianoRollView::mouseDrag (const juce::MouseEvent& e)
    {
        setView (dragStartView.x - e.getDistanceFromDragStartX() / pixelsPerBeat, pixelsPerBeat,
                 dragStartView.y + e.getDistanceFromDragStartY() / rowHeight, rowHeight);
    }
}
