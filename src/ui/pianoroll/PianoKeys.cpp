#include "ui/pianoroll/PianoKeys.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace colours = theme::pianoroll;

    PianoKeys::PianoKeys (PianoRollModel& m, View& v)
        : model (m), view (v)
    {
        setOpaque (true);
    }

    void PianoKeys::paint (juce::Graphics& g)
    {
        const auto area = g.getClipBounds();
        const auto width = (float) getWidth();
        const auto blackWidth = std::round (width * 0.62f);

        g.setColour (colours::whiteKey);
        g.fillRect (area);

        const auto topPitch = view.yToPitch ((float) area.getY());
        const auto bottomPitch = view.yToPitch ((float) area.getBottom());
        const auto showLabels = view.rowHeight >= 9.0f;
        const auto drumNames = showLabels && model.areDrumNamesShown();

        g.setFont (juce::FontOptions (std::min (theme::size::smallFontSize, view.rowHeight)));

        for (int pitch = bottomPitch; pitch <= topPitch; ++pitch)
        {
            const auto y = view.pitchToY (pitch);
            const juce::Rectangle<float> row { 0.0f, y, width, view.rowHeight };
            const auto note = pitch % 12;

            const auto lit = pitch == pressedPitch || model.isPitchHeld (pitch);

            const auto drum = drumNames ? model.getDrumName (pitch) : juce::String();

            if (juce::MidiMessage::isMidiNoteBlack (pitch))
            {
                g.setColour (lit ? theme::colour::accent : colours::blackKey);
                g.fillRect (row.withWidth (blackWidth));

                if (drum.isNotEmpty())
                {
                    g.setColour (theme::colour::textPrimary.withAlpha (0.9f));
                    g.drawText (drum, row.withWidth (blackWidth).withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
                }

                continue;
            }

            if (lit)
            {
                g.setColour (theme::colour::accent.withAlpha (0.6f));
                g.fillRect (row);
            }

            // Separators between white keys: under every C and F (where two white keys meet).
            if (note == 0 || note == 5)
            {
                g.setColour (colours::whiteKeyText.withAlpha (0.5f));
                g.fillRect (0.0f, y + view.rowHeight - 1.0f, width, 1.0f);
            }

            if (note == 0 && showLabels)
            {
                g.setColour (colours::whiteKeyText);
                g.drawText (juce::MidiMessage::getMidiNoteName (pitch, true, true, 4),
                            row.withTrimmedRight (4.0f), juce::Justification::centredRight, false);
            }

            if (drum.isNotEmpty())
            {
                g.setColour (colours::blackKey);
                g.drawText (drum, row.withTrimmedLeft (4.0f).withTrimmedRight (note == 0 ? 30.0f : 4.0f),
                            juce::Justification::centredLeft, true);
            }
        }

        // The scale's notes get a mark at the edge, the root a strong one.
        if (const auto scale = model.getScale(); ! scale.isChromatic())
        {
            for (int pitch = bottomPitch; pitch <= topPitch; ++pitch)
            {
                if (scale.contains (pitch))
                {
                    g.setColour (theme::colour::accent.withAlpha (scale.isRoot (pitch) ? 0.95f : 0.45f));
                    g.fillRect (width - 5.0f, view.pitchToY (pitch) + 1.0f, 4.0f, std::max (1.0f, view.rowHeight - 2.0f));
                }
            }
        }

        g.setColour (theme::colour::border);
        g.fillRect ((float) getWidth() - 1.0f, (float) area.getY(), 1.0f, (float) area.getHeight());
    }

    void PianoKeys::press (const juce::MouseEvent& e)
    {
        const auto pitch = view.yToPitch (e.position.y);
        const auto velocity = juce::jlimit (1, 127, juce::roundToInt (40.0f + 87.0f * e.position.x / (float) getWidth()));

        if (pitch != pressedPitch)
        {
            pressedPitch = pitch;
            model.auditionStart (pitch, velocity);
            repaint();
        }
    }

    juce::PopupMenu PianoKeys::createMenu()
    {
        juce::PopupMenu menu;
        const auto shown = model.areDrumNamesShown();
        menu.addItem ("Drum names (General MIDI)", model.getClip() != nullptr, shown,
                      [this, shown] { model.setDrumNamesShown (! shown); });
        return menu;
    }

    void PianoKeys::mouseDown (const juce::MouseEvent& e)
    {
        lastZoomY = e.position.y;

        if (e.mods.isPopupMenu())
        {
            createMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }

        if (e.mods.isLeftButtonDown())
            press (e);
    }

    void PianoKeys::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.mods.isMiddleButtonDown())
        {
            // Drag up to zoom in, down to zoom out, around the key where the drag started.
            const auto factor = std::pow (1.01f, lastZoomY - e.position.y);
            lastZoomY = e.position.y;
            view.zoomVertically (factor, (float) e.getMouseDownY());
            viewChanged();
            return;
        }

        if (e.mods.isLeftButtonDown())
            press (e);
    }

    void PianoKeys::mouseUp (const juce::MouseEvent&)
    {
        if (pressedPitch >= 0)
        {
            model.auditionStop();
            pressedPitch = -1;
            repaint();
        }
    }

    void PianoKeys::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (e.mods.isCtrlDown())
            view.zoomVertically (std::pow (2.0f, wheel.deltaY * 2.0f), e.position.y);
        else
            view.scrollByPixels (0.0, -wheel.deltaY * 250.0);

        viewChanged();
    }

    void PianoKeys::viewChanged()
    {
        if (onViewChanged != nullptr)
            onViewChanged();
    }
}
