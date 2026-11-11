#include "ui/automation/AutomationLane.h"
#include "ui/Theme.h"

namespace harmonia::ui::automation
{
    namespace automation = audio::automation;

    namespace
    {
        constexpr float minBarPixels = 6.0f, pointRadius = 4.0f, bendRadius = 3.0f;
        constexpr float curveStepPixels = 3.0f;     // a bent stretch is drawn as short straight ones
    }

    AutomationLane::AutomationLane (arrangement::ArrangementModel& m, const arrangement::ArrangementView& v)
        : model (m), view (v)
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    void AutomationLane::setParameter (te::AutomatableParameter* parameter)
    {
        if (parameter == gestures.getParameter())
            return;

        gestures.setParameter (parameter);
        hovered = hoveredBend = -1;
        repaint();
    }

    void AutomationLane::setLineColour (juce::Colour colour)
    {
        if (colour != lineColour)
        {
            lineColour = colour;
            repaint();
        }
    }

    void AutomationLane::setPlayheadBeat (std::optional<double> beat)
    {
        const auto repaintAt = [this] (std::optional<double> b)
        {
            if (b.has_value())
                repaint (juce::Rectangle<int> ((int) std::round (view.beatToX (*b)) - 2, 0, 5, getHeight()));
        };

        if (beat == playheadBeat)
            return;

        repaintAt (playheadBeat);
        playheadBeat = beat;
        repaintAt (playheadBeat);
    }

    void AutomationLane::resized()
    {
        gestures.setHeight ((float) getHeight());
    }

    //==============================================================================
    void AutomationLane::paint (juce::Graphics& g)
    {
        g.fillAll (theme::arrangement::laneEven);
        paintGrid (g);

        if (auto* parameter = gestures.getParameter())
            paintLine (g, *parameter);
        else
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText ("No track", getLocalBounds().reduced (theme::size::padding), juce::Justification::centredLeft);
        }

        if (playheadBeat.has_value())
        {
            g.setColour (theme::pianoroll::playhead);
            g.fillRect (std::round (view.beatToX (*playheadBeat)), 0.0f, 1.0f, (float) getHeight());
        }
    }

    void AutomationLane::paintGrid (juce::Graphics& g)
    {
        const auto beatsPerBar = model.getBeatsPerBar();
        const auto barPixels = (float) (beatsPerBar * view.pixelsPerBeat);
        const auto bottom = (float) getHeight();
        const auto firstBar = std::max (0, (int) std::floor (view.xToBeat (0.0f) / beatsPerBar));
        const auto lastBar = (int) std::ceil (view.xToBeat ((float) getWidth()) / beatsPerBar);

        if (barPixels >= 4.0f * minBarPixels)
        {
            g.setColour (theme::pianoroll::stepLine);

            for (int bar = firstBar; bar <= lastBar; ++bar)
                for (int beat = 1; beat < (int) beatsPerBar; ++beat)
                    g.fillRect (std::round (view.beatToX (bar * beatsPerBar + beat)), 0.0f, 1.0f, bottom);
        }

        int barStep = 1;

        while (barPixels * (float) barStep < minBarPixels)
            barStep *= 2;

        for (int bar = firstBar - firstBar % barStep; bar <= lastBar; bar += barStep)
        {
            g.setColour (bar % (barStep * 4) == 0 ? theme::pianoroll::barLine : theme::pianoroll::beatLine);
            g.fillRect (std::round (view.beatToX (bar * beatsPerBar)), 0.0f, 1.0f, bottom);
        }

        // The top and bottom of the range, and the quarters between.
        for (const auto value : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            g.setColour (value == 0.0f || value == 1.0f ? theme::pianoroll::beatLine : theme::pianoroll::stepLine);
            g.fillRect (0.0f, std::round (gestures.valueToY (value)), (float) getWidth(), 1.0f);
        }
    }

    void AutomationLane::paintLine (juce::Graphics& g, te::AutomatableParameter& parameter)
    {
        const auto points = automation::getPoints (parameter);
        const auto width = (float) getWidth();

        if (points.empty())
        {
            // No line: the value stays where the knob is.
            const auto y = gestures.valueToY (automation::getValueAt (parameter, 0.0));
            juce::Path flat;
            flat.startNewSubPath (0.0f, y);
            flat.lineTo (width, y);

            const float dashes[] = { 4.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, flat, dashes, 2);
            g.setColour (lineColour.withAlpha (0.6f));
            g.fillPath (dashed);

            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText ("Click to add points: " + automation::getDisplayName (parameter)
                            + " follows the line while the song plays. Right-click deletes a point; the small handle between two points bends the line.",
                        getLocalBounds().reduced (theme::size::padding, 0).removeFromBottom (24),
                        juce::Justification::centredLeft, true);
            return;
        }

        // Flat before the first point and after the last; between them straight, or bent.
        juce::Path line;
        line.startNewSubPath (std::min (0.0f, view.beatToX (points.front().beat)), gestures.valueToY (points.front().value));
        line.lineTo (view.beatToX (points.front().beat), gestures.valueToY (points.front().value));

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto from = view.beatToX (points[i].beat), to = view.beatToX (points[i + 1].beat);

            // A bent stretch as the parameter will go: its value every few pixels of what is in view.
            if (points[i].curve != 0.0f)
                for (auto x = std::max (from, -curveStepPixels) + curveStepPixels; x < std::min (to, width + curveStepPixels); x += curveStepPixels)
                    line.lineTo (x, gestures.valueToY (automation::getValueAt (parameter, view.xToBeat (x))));

            line.lineTo (to, gestures.valueToY (points[i + 1].value));
        }

        line.lineTo (std::max (width, view.beatToX (points.back().beat)), gestures.valueToY (points.back().value));

        auto fill = line;
        const auto bottom = (float) getHeight();
        fill.lineTo (std::max (width, view.beatToX (points.back().beat)), bottom);
        fill.lineTo (std::min (0.0f, view.beatToX (points.front().beat)), bottom);
        fill.closeSubPath();

        g.setColour (lineColour.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (lineColour);
        g.strokePath (line, juce::PathStrokeType (1.5f));

        // The handles the stretches are bent by: smaller than the points, and hollow.
        const auto bent = gestures.getBentStretch();

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto handle = gestures.getBendHandle ((int) i);

            if (! handle.has_value() || handle->x < -bendRadius || handle->x > width + bendRadius)
                continue;

            const auto circle = juce::Rectangle<float> (2.0f * bendRadius, 2.0f * bendRadius).withCentre (*handle);
            g.setColour (theme::arrangement::laneEven);
            g.fillEllipse (circle);
            g.setColour ((int) i == hoveredBend || (int) i == bent ? theme::pianoroll::noteSelected : lineColour);
            g.drawEllipse (circle, 1.5f);
        }

        const auto dragged = gestures.getDraggedPoint();

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto x = view.beatToX (points[i].beat);

            if (x < -pointRadius || x > width + pointRadius)
                continue;

            const auto highlighted = (int) i == hovered || (int) i == dragged;
            const auto circle = juce::Rectangle<float> (2.0f * pointRadius, 2.0f * pointRadius)
                                    .withCentre ({ x, gestures.valueToY (points[i].value) });

            g.setColour (highlighted ? theme::pianoroll::noteSelected : lineColour);
            g.fillEllipse (circle);
            g.setColour (theme::pianoroll::noteBorder);
            g.drawEllipse (circle, 1.0f);
        }

        const auto shown = dragged >= 0 ? dragged : hovered;
        const auto shownBend = bent >= 0 ? bent : hoveredBend;

        if (juce::isPositiveAndBelow (shown, (int) points.size()))
        {
            const auto& point = points[(size_t) shown];
            paintReadout (g, describePoint (parameter, point), { view.beatToX (point.beat), gestures.valueToY (point.value) });
        }
        else if (const auto handle = gestures.getBendHandle (shownBend))
        {
            const auto bend = juce::roundToInt (100.0f * points[(size_t) shownBend].curve);
            paintReadout (g, bend == 0 ? juce::String ("Straight: drag up or down to bend")
                                       : "Bend " + juce::String (std::abs (bend)) + "%: " + (bend > 0 ? "late" : "early"),
                          *handle);
        }
    }

    juce::String AutomationLane::describePoint (te::AutomatableParameter& parameter, const automation::Point& point) const
    {
        const auto beatsPerBar = model.getBeatsPerBar();
        const auto bar = (int) std::floor (point.beat / beatsPerBar);
        const auto beatInBar = point.beat - bar * beatsPerBar;

        return automation::valueToText (parameter, point.value) + juce::String::fromUTF8 ("  \xc2\xb7  ")
             + "bar " + juce::String (bar + 1) + "." + juce::String (beatInBar + 1.0, beatInBar == std::floor (beatInBar) ? 0 : 2);
    }

    void AutomationLane::paintReadout (juce::Graphics& g, const juce::String& text, juce::Point<float> at)
    {
        const juce::Font font { juce::FontOptions (theme::size::smallFontSize) };
        const auto textWidth = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;

        auto box = juce::Rectangle<float> (textWidth, 18.0f).withPosition (at.x + 8.0f, at.y - 24.0f);

        if (box.getRight() > (float) getWidth())
            box.setX (at.x - 8.0f - textWidth);

        box.setY (juce::jlimit (0.0f, std::max (0.0f, (float) getHeight() - box.getHeight()), box.getY() < 0.0f ? at.y + 8.0f : box.getY()));

        g.setColour (theme::colour::surfaceRaised.withAlpha (0.95f));
        g.fillRoundedRectangle (box, theme::size::cornerRadius);
        g.setColour (theme::colour::textPrimary);
        g.setFont (font);
        g.drawText (text, box, juce::Justification::centred, false);
    }

    //==============================================================================
    AutomationGestures::Pointer AutomationLane::toPointer (const juce::MouseEvent& e) const
    {
        return { e.position, e.mods, e.getNumberOfClicks() };
    }

    void AutomationLane::mouseDown (const juce::MouseEvent& e)
    {
        gestures.down (toPointer (e));
        hovered = gestures.getDraggedPoint();
        hoveredBend = gestures.getBentStretch();
        setMouseCursor (gestures.getCursorFor (e.position));
        repaint();

        if (onEdited != nullptr)
            onEdited();
    }

    void AutomationLane::mouseDrag (const juce::MouseEvent& e)
    {
        gestures.drag (toPointer (e));
        repaint();

        if (onEdited != nullptr)
            onEdited();
    }

    void AutomationLane::mouseUp (const juce::MouseEvent& e)
    {
        gestures.up (toPointer (e));
        updateHover (e.position);
        repaint();
    }

    void AutomationLane::mouseMove (const juce::MouseEvent& e)
    {
        updateHover (e.position);
    }

    void AutomationLane::mouseExit (const juce::MouseEvent&)
    {
        if (hovered >= 0 || hoveredBend >= 0)
        {
            hovered = hoveredBend = -1;
            repaint();
        }
    }

    void AutomationLane::updateHover (juce::Point<float> position)
    {
        const auto index = gestures.pointAt (position);
        const auto bend = index >= 0 ? -1 : gestures.bendAt (position);
        setMouseCursor (gestures.getCursorFor (position));

        if (index != hovered || bend != hoveredBend)
        {
            hovered = index;
            hoveredBend = bend;
            repaint();
        }
    }
}
