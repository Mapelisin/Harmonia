#include "ui/settings/MidiSettingsTab.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    MidiSettingsTab::MidiSettingsTab (Source s)
        : source (std::move (s))
    {
        title.setText ("MIDI keyboards", juce::dontSendNotification);
        title.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);

        message.setFont (juce::FontOptions (theme::size::smallFontSize));
        message.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        message.setJustificationType (juce::Justification::topLeft);
        message.setMinimumHorizontalScale (1.0f);

        addAndMakeVisible (title);
        addAndMakeVisible (message);
        refresh();
    }

    void MidiSettingsTab::refresh()
    {
        toggles.clear();
        const auto keyboards = source.getKeyboards != nullptr ? source.getKeyboards() : std::vector<audio::MidiInputs::Keyboard>();

        for (const auto& keyboard : keyboards)
        {
            auto* toggle = toggles.add (std::make_unique<juce::ToggleButton> (keyboard.name));
            toggle->setToggleState (keyboard.on, juce::dontSendNotification);
            toggle->onClick = [this, id = keyboard.id, toggle]
            {
                if (source.setOn != nullptr)
                    source.setOn (id, toggle->getToggleState());
            };
            addAndMakeVisible (toggle);
        }

        message.setText (keyboards.empty() ? "No MIDI keyboard is plugged in. Plug one in (USB): it shows up here and plays "
                                             "the track being edited, no setup needed."
                                           : "A keyboard that's on plays the track being edited (its notes are recorded with R). "
                                             "Switch off one that sends what it shouldn't.",
                         juce::dontSendNotification);
        resized();
    }

    void MidiSettingsTab::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
    }

    void MidiSettingsTab::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding * 2);
        title.setBounds (area.removeFromTop (theme::size::buttonHeight));
        area.removeFromTop (theme::size::padding);

        for (auto* toggle : toggles)
            toggle->setBounds (area.removeFromTop (theme::size::buttonHeight + 4));

        area.removeFromTop (theme::size::padding);
        message.setBounds (area.removeFromTop (48));
    }
}
