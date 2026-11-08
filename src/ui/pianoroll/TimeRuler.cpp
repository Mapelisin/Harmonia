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
        constexpr float flagPadding = 4.0f;
        constexpr float loopHandleReach = 5.0f;

        juce::FontOptions markerFont()
        {
            return juce::FontOptions (theme::size::smallFontSize, juce::Font::bold);
        }
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

        paintClipStrip (g, bounds);
        paintMarkers (g);

        // Bars (below the markers): label every 1, 2, 4… bars so numbers never overlap; beat ticks when there's room.
        const auto barsTop = markerLaneHeight;
        const auto barsHeight = bounds.getHeight() - barsTop;
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
                g.fillRect (x, barsTop + barsHeight * 0.35f, 1.0f, barsHeight * 0.65f);
                g.setColour (theme::colour::textSecondary);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, barsTop, 40.0f, barsHeight - clipStripHeight),
                            juce::Justification::centredLeft, false);
            }

            if (view.pixelsPerBeat >= 12.0)
            {
                g.setColour (colours::beatLine);

                for (int beat = 1; beat < (int) beatsPerBar; ++beat)
                    g.fillRect (std::round (view.beatToX (bar * beatsPerBar + beat)), barsTop + barsHeight * 0.7f,
                                1.0f, barsHeight * 0.3f);
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

    void TimeRuler::paintClipStrip (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (model.getClip() == nullptr)
            return;

        const auto strip = [&] (double start, double end)
        {
            return juce::Rectangle<float>::leftTopRightBottom (view.beatToX (start), bounds.getBottom() - clipStripHeight,
                                                               view.beatToX (end), bounds.getBottom());
        };

        const auto loopStart = model.getClipStartBeat(), loopEnd = model.getClipEndBeat();

        if (model.isLooping())
        {
            // The repeats: fainter, with a mark where each time round begins.
            const auto whole = model.getWholeClipBeats();
            g.setColour (colours::clipRange.withAlpha (0.45f));
            g.fillRect (strip (whole.getStart(), whole.getEnd()));

            const auto length = loopEnd - loopStart;

            if (length * view.pixelsPerBeat >= 4.0)
            {
                g.setColour (colours::clipRange.brighter (0.4f));
                const auto first = std::ceil ((whole.getStart() - loopStart) / length - 1.0e-9);

                for (auto beat = loopStart + first * length; beat < whole.getEnd() - 1.0e-9; beat += length)
                    if (beat > whole.getStart() + 1.0e-9)
                        g.fillRect (std::round (view.beatToX (beat)), bounds.getBottom() - clipStripHeight * 2.5f, 1.0f, clipStripHeight * 2.5f);
            }
        }

        g.setColour (colours::clipRange);
        g.fillRect (strip (loopStart, loopEnd));

        if (auto x = getLoopEndX())
        {
            // The handle: a bracket closing the loop.
            const auto top = markerLaneHeight + 3.0f;
            g.setColour (theme::colour::accent);
            g.fillRect (*x - 2.0f, top, 2.0f, bounds.getBottom() - top);
            g.fillRect (*x - 7.0f, top, 7.0f, 2.0f);
            g.fillRect (*x - 7.0f, bounds.getBottom() - 2.0f, 7.0f, 2.0f);
        }
    }

    juce::Rectangle<float> TimeRuler::getFlag (const core::markers::Marker& marker) const
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidth (juce::Font (markerFont()), marker.name);
        return { view.beatToX (marker.beat), 1.0f, textWidth + flagPadding * 2.0f, markerLaneHeight - 2.0f };
    }

    void TimeRuler::paintMarkers (juce::Graphics& g)
    {
        const auto markers = model.getMarkers();
        g.setFont (markerFont());

        for (size_t i = 0; i < markers.size(); ++i)
        {
            auto flag = getFlag (markers[i]);

            // Not over the next marker's flag.
            if (i + 1 < markers.size())
                flag.setRight (std::max (flag.getX() + 6.0f, std::min (flag.getRight(), view.beatToX (markers[i + 1].beat) - 2.0f)));

            if (flag.getRight() < 0.0f || flag.getX() > (float) getWidth())
                continue;

            g.setColour (theme::colour::accent.withAlpha (0.8f));
            g.fillRoundedRectangle (flag, 3.0f);
            g.fillRect (flag.getX(), 1.0f, 1.0f, (float) getHeight() - 1.0f);
            g.setColour (theme::colour::background);
            g.drawText (markers[i].name, flag.withTrimmedLeft (flagPadding), juce::Justification::centredLeft, true);
        }
    }

    std::optional<core::markers::Marker> TimeRuler::findMarkerAt (juce::Point<float> position) const
    {
        if (position.y >= markerLaneHeight)
            return std::nullopt;

        // The last one wins: a later marker's flag is drawn over an earlier one's.
        std::optional<core::markers::Marker> found;

        for (const auto& marker : model.getMarkers())
            if (getFlag (marker).expanded (2.0f, 0.0f).contains (position.x, 2.0f))
                found = marker;

        return found;
    }

    std::optional<float> TimeRuler::getLoopEndX() const
    {
        if (! model.isLooping())
            return std::nullopt;

        return view.beatToX (model.getClipEndBeat());
    }

    bool TimeRuler::isOverLoopEnd (juce::Point<float> position) const
    {
        const auto x = getLoopEndX();
        return x.has_value() && position.y >= markerLaneHeight && std::abs (position.x - *x) <= loopHandleReach;
    }

    double TimeRuler::snapped (const juce::MouseEvent& e) const
    {
        const auto mode = e.mods.isAltDown() ? snap::Mode::none : model.getSnap();
        return std::max (0.0, snap::snapNearest (view.xToBeat (e.position.x), mode, model.getSnapContext()));
    }

    void TimeRuler::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (isOverLoopEnd (e.position)               ? juce::MouseCursor::LeftRightResizeCursor
                        : findMarkerAt (e.position).has_value()  ? juce::MouseCursor::PointingHandCursor
                                                                 : juce::MouseCursor::NormalCursor);
    }

    void TimeRuler::mouseDown (const juce::MouseEvent& e)
    {
        selectingTime = draggingLoopEnd = false;

        if (! e.mods.isCtrlDown() && isOverLoopEnd (e.position))
        {
            draggingLoopEnd = true;
            loopStartBeat = model.getClipStartBeat();
            model.beginGesture ("Loop length");
            return;
        }

        if (! e.mods.isCtrlDown())
        {
            if (const auto marker = findMarkerAt (e.position))
            {
                if (onPositionChosen != nullptr)
                    onPositionChosen (marker->beat);

                return;
            }
        }

        selectingTime = e.mods.isCtrlDown() || e.getNumberOfClicks() >= 2;
        selectionAnchor = snapped (e);

        if (! selectingTime && onPositionChosen != nullptr)
            onPositionChosen (selectionAnchor);
    }

    void TimeRuler::mouseDrag (const juce::MouseEvent& e)
    {
        if (draggingLoopEnd)
        {
            // At least one grid step (or a 16th without the grid).
            const auto step = std::max (1.0 / 16.0, snap::getStepBeats (model.getSnap(), model.getSnapContext()));
            model.setLoopLength (std::max (snapped (e) - loopStartBeat, e.mods.isAltDown() ? 1.0 / 16.0 : step));
            repaint();
        }
        else if (findMarkerAt (e.mouseDownPosition).has_value() && ! e.mods.isCtrlDown())
        {
            // A click on a marker: no drag (markers are moved in the arrangement).
        }
        else if (selectingTime)
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

        selectingTime = draggingLoopEnd = false;
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
