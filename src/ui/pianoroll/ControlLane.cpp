#include "ui/pianoroll/ControlLane.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace colours = theme::pianoroll;
    namespace property = core::note_property;

    namespace
    {
        constexpr float margin = 6.0f;
        constexpr float knobRadius = 3.0f;
        constexpr float hitTolerancePixels = 4.0f;
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

        if (hoverText.isNotEmpty())
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.drawText (hoverText, getLocalBounds().reduced (6, 2), juce::Justification::topRight, false);
        }

        g.setColour (theme::colour::border);
        g.fillRect (area.getX(), 0, area.getWidth(), 1);
    }

    void ControlLane::applySwipe (const juce::MouseEvent& e)
    {
        const auto beat = view.xToBeat (e.position.x);
        const auto value = e.mods.isAltDown() ? property::getDefault (model.getLaneProperty()) : valueAt (e);
        const auto tolerance = hitTolerancePixels / view.pixelsPerBeat;

        model.setLaneValues (lastBeat, lastValue, beat, value, tolerance);

        lastBeat = beat;
        lastValue = value;
    }

    void ControlLane::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isLeftButtonDown())
            return;

        editing = true;
        model.beginGesture ("Change " + property::getName (model.getLaneProperty()).toLowerCase());
        lastBeat = view.xToBeat (e.position.x);
        lastValue = e.mods.isAltDown() ? property::getDefault (model.getLaneProperty()) : valueAt (e);
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
        editing = false;
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
        // Show the value of the nearest editable note under the mouse.
        const auto beat = view.xToBeat (e.position.x);
        const auto tolerance = hitTolerancePixels / view.pixelsPerBeat;
        const auto& notes = model.getNotes();
        const auto [first, last] = model.getNotesInRange (beat - tolerance, beat + tolerance);

        juce::String text;

        for (auto i = first; i < last; ++i)
        {
            const auto& d = notes[i];

            if (std::abs (d.values.startBeat - beat) <= tolerance && model.isLaneEditable (d))
            {
                text = juce::MidiMessage::getMidiNoteName (d.values.pitch, true, true, 4) + "  "
                     + property::describe (d.values, model.getLaneProperty());
                break;
            }
        }

        if (text != hoverText)
        {
            hoverText = text;
            repaint();
        }
    }
}
