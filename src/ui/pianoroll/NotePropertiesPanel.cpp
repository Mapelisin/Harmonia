#include "ui/pianoroll/NotePropertiesPanel.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace
    {
        constexpr int rowHeight = 24;
        constexpr int labelWidth = 80;
        constexpr int panelWidth = 320;
    }

    NotePropertiesPanel::NotePropertiesPanel (PianoRollModel& m, te::MidiNote& note)
        : model (m)
    {
        targets = model.isSelected (note) ? model.getSelectedNotes() : juce::Array<te::MidiNote*> { &note };
        shown = model.read (note);

        model.beginGesture ("Note properties");

        auto title = juce::MidiMessage::getMidiNoteName (shown.pitch, true, true, 4);

        if (targets.size() > 1)
            title << "  (and " << (targets.size() - 1) << " more selected)";

        titleLabel.setText (title, juce::dontSendNotification);
        titleLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        addAndMakeVisible (titleLabel);

        setUpRow (rows[0], "Velocity", 0, 127, 1, shown.velocity,
                  [] (core::Note& n, double v) { n.velocity = juce::roundToInt (v); });
        setUpRow (rows[1], "Release", 0, 127, 1, shown.releaseVelocity,
                  [] (core::Note& n, double v) { n.releaseVelocity = juce::roundToInt (v); });
        setUpRow (rows[2], "Pan", -100, 100, 1, shown.pan * 100.0,
                  [] (core::Note& n, double v) { n.pan = (float) (v / 100.0); });
        setUpRow (rows[3], "Mod X", 0, 100, 1, shown.modX * 100.0,
                  [] (core::Note& n, double v) { n.modX = (float) (v / 100.0); });
        setUpRow (rows[4], "Mod Y", 0, 100, 1, shown.modY * 100.0,
                  [] (core::Note& n, double v) { n.modY = (float) (v / 100.0); });
        setUpRow (rows[5], "Fine pitch", -core::note::maxFinePitchCents, core::note::maxFinePitchCents, 1,
                  shown.finePitchCents, [] (core::Note& n, double v) { n.finePitchCents = (float) v; });

        rows[3].slider.setTextValueSuffix ("%");
        rows[4].slider.setTextValueSuffix ("%");
        rows[5].slider.setTextValueSuffix (" ct");

        const auto setUpToggle = [this] (juce::ToggleButton& button, bool value, std::function<void (core::Note&, bool)> set)
        {
            button.setToggleState (value, juce::dontSendNotification);
            button.onClick = [this, &button, set]
            {
                const auto on = button.getToggleState();
                apply ([&] (core::Note& n) { set (n, on); });
            };
            addAndMakeVisible (button);
        };

        setUpToggle (muteButton, shown.muted, [] (core::Note& n, bool on) { n.muted = on; });
        setUpToggle (slideButton, shown.slide, [] (core::Note& n, bool on) { n.slide = on; });
        setUpToggle (portaButton, shown.porta, [] (core::Note& n, bool on) { n.porta = on; });

        colourLabel.setText ("Colour", juce::dontSendNotification);
        addAndMakeVisible (colourLabel);

        for (int i = 0; i < core::note::numColours; ++i)
            colourBox.addItem ("Colour " + juce::String (i + 1) + "  (MIDI channel " + juce::String (i + 1) + ")", i + 1);

        colourBox.setSelectedId (shown.colour + 1, juce::dontSendNotification);
        colourBox.onChange = [this]
        {
            const auto colour = colourBox.getSelectedId() - 1;
            apply ([colour] (core::Note& n) { n.colour = colour; });
        };
        addAndMakeVisible (colourBox);

        setSize (panelWidth, theme::size::padding * 2 + rowHeight * 9);
    }

    void NotePropertiesPanel::setUpRow (Row& row, const juce::String& name, double min, double max, double interval,
                                        double value, std::function<void (core::Note&, double)> applyValue)
    {
        row.label.setText (name, juce::dontSendNotification);
        row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, rowHeight - 4);
        row.slider.setRange (min, max, interval);
        row.slider.setValue (value, juce::dontSendNotification);
        row.slider.setDoubleClickReturnValue (true, value);
        row.slider.onValueChange = [this, &row, applyValue]
        {
            const auto v = row.slider.getValue();
            apply ([&] (core::Note& n) { applyValue (n, v); });
        };

        addAndMakeVisible (row.label);
        addAndMakeVisible (row.slider);
    }

    void NotePropertiesPanel::apply (const std::function<void (core::Note&)>& change)
    {
        std::vector<core::NoteEditor::Change> changes;

        for (auto* n : targets)
        {
            auto v = model.read (*n);
            change (v);
            changes.push_back ({ n, v });
        }

        model.updateNotes (changes);
    }

    void NotePropertiesPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::surface);
    }

    void NotePropertiesPanel::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding);
        titleLabel.setBounds (area.removeFromTop (rowHeight));

        for (auto& row : rows)
        {
            auto line = area.removeFromTop (rowHeight);
            row.label.setBounds (line.removeFromLeft (labelWidth));
            row.slider.setBounds (line);
        }

        auto toggles = area.removeFromTop (rowHeight);
        const auto toggleWidth = toggles.getWidth() / 3;
        muteButton.setBounds (toggles.removeFromLeft (toggleWidth));
        slideButton.setBounds (toggles.removeFromLeft (toggleWidth));
        portaButton.setBounds (toggles);

        auto colourRow = area.removeFromTop (rowHeight);
        colourLabel.setBounds (colourRow.removeFromLeft (labelWidth));
        colourBox.setBounds (colourRow.reduced (0, 1));
    }

    void NotePropertiesPanel::show (PianoRollModel& model, te::MidiNote& note, juce::Component& parent,
                                    juce::Rectangle<int> area)
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<NotePropertiesPanel> (model, note), area, &parent);
    }
}
