#include "ui/automation/AutomationLane.h"
#include "ui/Theme.h"

namespace harmonia::ui::automation
{
    namespace automation = audio::automation;

    namespace
    {
        constexpr float minBarPixels = 6.0f, pointRadius = 4.0f;
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
        hovered = -1;
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
                            + " follows the line while the song plays. Right-click deletes a point.",
                        getLocalBounds().reduced (theme::size::padding, 0).removeFromBottom (24),
                        juce::Justification::centredLeft, true);
            return;
        }

        // Flat before the first point and after the last, straight between them.
        juce::Path line;
        line.startNewSubPath (std::min (0.0f, view.beatToX (points.front().beat)), gestures.valueToY (points.front().value));

        for (const auto& point : points)
            line.lineTo (view.beatToX (point.beat), gestures.valueToY (point.value));

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

        if (juce::isPositiveAndBelow (shown, (int) points.size()))
            paintReadout (g, parameter, shown);
    }

    void AutomationLane::paintReadout (juce::Graphics& g, te::AutomatableParameter& parameter, int pointIndex)
    {
        const auto point = automation::getPoints (parameter)[(size_t) pointIndex];
        const auto beatsPerBar = model.getBeatsPerBar();
        const auto bar = (int) std::floor (point.beat / beatsPerBar);
        const auto beatInBar = point.beat - bar * beatsPerBar;

        const auto text = automation::valueToText (parameter, point.value) + juce::String::fromUTF8 ("  \xc2\xb7  ")
                        + "bar " + juce::String (bar + 1) + "." + juce::String (beatInBar + 1.0, beatInBar == std::floor (beatInBar) ? 0 : 2);

        const juce::Font font { juce::FontOptions (theme::size::smallFontSize) };
        const auto textWidth = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;
        const juce::Point<float> at { view.beatToX (point.beat), gestures.valueToY (point.value) };

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
        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void AutomationLane::updateHover (juce::Point<float> position)
    {
        const auto index = gestures.pointAt (position);
        setMouseCursor (gestures.getCursorFor (position));

        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }
}
