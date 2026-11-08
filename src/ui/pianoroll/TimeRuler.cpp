#include "ui/pianoroll/TimeRuler.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace colours = theme::pianoroll;
    namespace snap = core::snap;

    namespace
    {
        constexpr float minBarLabelSpacing = 36.0f;
        constexpr float clipStripHeight = 4.0f;
        constexpr float playheadSize = 9.0f;
    }

    TimeRuler::TimeRuler (PianoRollModel& m, View& v)
        : model (m), view (v)
    {
        setOpaque (true);
    }

    void TimeRuler::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.fillAll (theme::colour::surface);

        if (auto range = model.getTimeSelection())
        {
            g.setColour (theme::colour::accent.withAlpha (0.35f));
            g.fillRect (bounds.withLeft (view.beatToX (range->start)).withRight (view.beatToX (range->end)));
        }

        if (model.getClip() != nullptr)
        {
            g.setColour (colours::clipRange);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (view.beatToX (model.getClipStartBeat()),
                                                                    bounds.getBottom() - clipStripHeight,
                                                                    view.beatToX (model.getClipEndBeat()),
                                                                    bounds.getBottom()));
        }

        // Bars: label every 1, 2, 4… bars so numbers never overlap; beat ticks when there's room.
        const auto beatsPerBar = model.getBeatsPerBar();
        const auto barWidth = (float) (beatsPerBar * view.pixelsPerBeat);
        int labelEvery = 1;

        while (barWidth * (float) labelEvery < minBarLabelSpacing)
            labelEvery *= 2;

        const auto firstBar = (int) std::floor (view.leftBeat / beatsPerBar);
        const auto lastBar = (int) std::ceil (view.getRightBeat() / beatsPerBar);

        g.setFont (juce::FontOptions (theme::size::smallFontSize));

        for (int bar = std::max (0, firstBar); bar <= lastBar; ++bar)
        {
            const auto x = std::round (view.beatToX (bar * beatsPerBar));

            if (bar % labelEvery == 0)
            {
                g.setColour (colours::barLine);
                g.fillRect (x, bounds.getHeight() * 0.35f, 1.0f, bounds.getHeight() * 0.65f);
                g.setColour (theme::colour::textSecondary);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, 0.0f, 40.0f, bounds.getHeight() - clipStripHeight),
                            juce::Justification::centredLeft, false);
            }

            if (view.pixelsPerBeat >= 12.0)
            {
                g.setColour (colours::beatLine);

                for (int beat = 1; beat < (int) beatsPerBar; ++beat)
                    g.fillRect (std::round (view.beatToX (bar * beatsPerBar + beat)), bounds.getHeight() * 0.7f,
                                1.0f, bounds.getHeight() * 0.3f);
            }
        }

        if (playheadBeat.has_value())
        {
            const auto x = view.beatToX (*playheadBeat);
            juce::Path triangle;
            triangle.addTriangle (x - playheadSize * 0.5f, bounds.getBottom() - playheadSize,
                                  x + playheadSize * 0.5f, bounds.getBottom() - playheadSize,
                                  x, bounds.getBottom());
            g.setColour (colours::playhead);
            g.fillPath (triangle);
        }

        g.setColour (theme::colour::border);
        g.fillRect (0.0f, bounds.getBottom() - 1.0f, bounds.getWidth(), 1.0f);
    }

    double TimeRuler::snapped (const juce::MouseEvent& e) const
    {
        const auto mode = e.mods.isAltDown() ? snap::Mode::none : model.getSnap();
        return std::max (0.0, snap::snapNearest (view.xToBeat (e.position.x), mode, model.getSnapContext()));
    }

    void TimeRuler::mouseDown (const juce::MouseEvent& e)
    {
        selectingTime = e.mods.isCtrlDown() || e.getNumberOfClicks() >= 2;
        selectionAnchor = snapped (e);

        if (! selectingTime && onPositionChosen != nullptr)
            onPositionChosen (selectionAnchor);
    }

    void TimeRuler::mouseDrag (const juce::MouseEvent& e)
    {
        if (selectingTime)
        {
            model.setTimeSelection ({ selectionAnchor, snapped (e) });
            repaint();
        }
        else if (onPositionChosen != nullptr)
        {
            onPositionChosen (snapped (e));
        }
    }

    void TimeRuler::mouseUp (const juce::MouseEvent& e)
    {
        if (selectingTime && ! e.mouseWasDraggedSinceMouseDown())
            model.clearTimeSelection();

        selectingTime = false;
        repaint();
    }

    void TimeRuler::setPlayheadBeat (std::optional<double> beat)
    {
        if (beat.has_value() && playheadBeat.has_value()
            && std::abs (view.beatToX (*beat) - view.beatToX (*playheadBeat)) < 0.5f)
            return;

        repaint (getPlayheadArea());
        playheadBeat = beat;
        repaint (getPlayheadArea());
    }

    juce::Rectangle<int> TimeRuler::getPlayheadArea() const
    {
        if (! playheadBeat.has_value())
            return {};

        const auto x = (int) std::round (view.beatToX (*playheadBeat));
        const auto half = (int) playheadSize;
        return { x - half, 0, half * 2, getHeight() };
    }
}
