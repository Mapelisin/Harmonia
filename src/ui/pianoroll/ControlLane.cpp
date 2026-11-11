#include "ui/pianoroll/ControlLane.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace colours = theme::pianoroll;
    namespace property = core::note_property;
    namespace controllers = core::controllers;

    namespace
    {
        constexpr float margin = 6.0f;
        constexpr float knobRadius = 3.0f;
        constexpr float grabRadiusPixels = 20.0f;   // how far from a stem the mouse may be and still hold it
        constexpr float maxTailPixels = 160.0f;
    }

    ControlLane::ControlLane (PianoRollModel& m, View& v)
        : model (m), view (v)
    {
        setOpaque (true);
    }

    float ControlLane::valueToY (float value) const
    {
        const auto top = margin, bottom = (float) getHeight() - margin;
        return bottom - value * (bottom - top);
    }

    float ControlLane::valueAt (const juce::MouseEvent& e) const
    {
        const auto top = margin, bottom = (float) getHeight() - margin;
        return juce::jlimit (0.0f, 1.0f, (bottom - e.position.y) / (bottom - top));
    }

    float ControlLane::drawnValue (const juce::MouseEvent& e) const
    {
        // Alt: the resting value (the bend in the middle, the note value's default).
        if (! e.mods.isAltDown())
            return valueAt (e);

        if (const auto c = model.getLaneController())
            return controllers::getDefault (*c);

        return property::getDefault (model.getLaneProperty());
    }

    void ControlLane::paint (juce::Graphics& g)
    {
        const auto area = g.getClipBounds();
        g.setColour (theme::colour::background);
        g.fillRect (area);

        // Bar lines, so values line up with the notes above.
        const auto beatsPerBar = model.getBeatsPerBar();
        g.setColour (colours::stepLine);

        for (auto bar = std::floor (view.leftBeat / beatsPerBar) * beatsPerBar; bar <= view.getRightBeat(); bar += beatsPerBar)
            g.fillRect (std::round (view.beatToX (bar)), 0.0f, 1.0f, (float) getHeight());

        if (const auto c = model.getLaneController())
            paintController (g, *c, area);
        else
            paintNotes (g, area);

        if (hoverText.isNotEmpty())
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.drawText (hoverText, getLocalBounds().reduced (6, 2), juce::Justification::topRight, false);
        }

        g.setColour (theme::colour::border);
        g.fillRect (area.getX(), 0, area.getWidth(), 1);
    }

    void ControlLane::paintNotes (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto laneProperty = model.getLaneProperty();
        const auto baseline = valueToY (property::isBipolar (laneProperty) ? 0.5f : 0.0f);

        if (property::isBipolar (laneProperty))
        {
            g.setColour (colours::beatLine);
            g.fillRect (0.0f, baseline, (float) getWidth(), 1.0f);
        }

        const auto& notes = model.getNotes();
        const auto [first, last] = model.getNotesInRange (view.xToBeat ((float) area.getX() - knobRadius),
                                                          view.xToBeat ((float) area.getRight() + knobRadius));
        const auto anySelected = model.hasSelection();

        for (auto i = first; i < last; ++i)
        {
            const auto& d = notes[i];
            const auto x = view.beatToX (d.values.startBeat);
            const auto tailEnd = std::min (view.beatToX (d.values.getEndBeat()), x + maxTailPixels);

            if (tailEnd < (float) area.getX() - knobRadius)
                continue;

            const auto y = valueToY (property::get (d.values, laneProperty));
            auto colour = d.selected ? colours::noteSelected
                                     : (d.values.muted ? colours::noteMuted : colours::getNoteColour (d.values.colour));

            if (anySelected && ! d.selected)
                colour = colour.withMultipliedAlpha (0.35f);

            g.setColour (colour.withMultipliedAlpha (0.45f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, y - 0.5f, std::max (x + 1.0f, tailEnd), y + 0.5f));

            g.setColour (colour);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, std::min (y, baseline), x + 1.0f, std::max (y, baseline)));
            g.fillEllipse (x - knobRadius + 0.5f, y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
        }
    }

    void ControlLane::paintController (juce::Graphics& g, controllers::Controller c, juce::Rectangle<int> area)
    {
        const auto baseline = valueToY (controllers::isBipolar (c) ? controllers::getDefault (c) : 0.0f);

        if (controllers::isBipolar (c))
        {
            g.setColour (colours::beatLine);
            g.fillRect (0.0f, baseline, (float) getWidth(), 1.0f);
        }

        // Only the clip has the controller: outside it is shaded, as in the notes above.
        const auto clipLeft = view.beatToX (model.getClipStartBeat());
        const auto clipRight = view.beatToX (model.getClipEndBeat());
        const auto left = std::max ((float) area.getX(), clipLeft);
        const auto right = std::min ((float) area.getRight(), clipRight);

        g.setColour (colours::outsideClip);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom ((float) area.getX(), 0.0f, std::max ((float) area.getX(), clipLeft), (float) getHeight()));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min ((float) area.getRight(), clipRight), 0.0f, (float) area.getRight(), (float) getHeight()));

        if (model.getClip() == nullptr || right <= left)
            return;

        // Each value holds until the next event: a line of steps.
        const auto leftBeat = view.xToBeat (left), rightBeat = view.xToBeat (right);
        auto y = valueToY (model.getControllerValueAt (c, leftBeat));
        juce::Path line;
        line.startNewSubPath (left, y);

        for (const auto& p : model.getControllerPoints (c))
        {
            if (p.beat <= leftBeat)
                continue;

            if (p.beat >= rightBeat)
                break;

            const auto x = view.beatToX (p.beat);
            line.lineTo (x, y);
            y = valueToY (p.value);
            line.lineTo (x, y);
        }

        line.lineTo (right, y);

        auto fill = line;
        fill.lineTo (right, baseline);
        fill.lineTo (left, baseline);
        fill.closeSubPath();

        g.setColour (theme::colour::accent.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (theme::colour::accent);
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

    void ControlLane::applySwipe (const juce::MouseEvent& e)
    {
        const auto beat = view.xToBeat (e.position.x);
        const auto value = drawnValue (e);

        if (! model.getLaneController().has_value())
            model.setLaneValues (lastBeat, lastValue, beat, value, grabRadiusPixels / view.pixelsPerBeat);
        else if (erasing)
            model.eraseControllers (lastBeat, beat);
        else
            model.drawControllerLine (lastBeat, lastValue, beat, value);

        lastBeat = beat;
        lastValue = value;
    }

    void ControlLane::mouseDown (const juce::MouseEvent& e)
    {
        const auto controller = model.getLaneController();
        erasing = controller.has_value() && e.mods.isRightButtonDown();

        if (! e.mods.isLeftButtonDown() && ! erasing)
            return;

        editing = true;

        if (controller.has_value())
            model.beginGesture ((erasing ? "Erase " : "Draw ") + controllers::getName (*controller).toLowerCase());
        else
            model.beginGesture ("Change " + property::getName (model.getLaneProperty()).toLowerCase());

        lastBeat = view.xToBeat (e.position.x);
        lastValue = drawnValue (e);
        applySwipe (e);
        updateHoverText (e);
    }

    void ControlLane::mouseDrag (const juce::MouseEvent& e)
    {
        if (editing)
        {
            applySwipe (e);
            updateHoverText (e);
        }
    }

    void ControlLane::mouseUp (const juce::MouseEvent&)
    {
        editing = erasing = false;
    }

    void ControlLane::mouseMove (const juce::MouseEvent& e)
    {
        updateHoverText (e);
    }

    void ControlLane::mouseExit (const juce::MouseEvent&)
    {
        hoverText.clear();
        repaint();
    }

    void ControlLane::updateHoverText (const juce::MouseEvent& e)
    {
        juce::String text;

        if (const auto c = model.getLaneController())
        {
            // What a click there would set.
            text = controllers::getName (*c) + "  " + controllers::describe (*c, drawnValue (e));
        }
        else if (const auto* d = model.findLaneNoteNear (view.xToBeat (e.position.x), grabRadiusPixels / view.pixelsPerBeat))
        {
            // The value of the note the mouse would grab.
            text = juce::MidiMessage::getMidiNoteName (d->values.pitch, true, true, 4) + "  "
                 + property::describe (d->values, model.getLaneProperty());
        }

        if (text != hoverText)
        {
            hoverText = text;
            repaint();
        }
    }
}
