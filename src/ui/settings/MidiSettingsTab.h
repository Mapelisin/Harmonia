#pragma once

#include "audio/MidiInputs.h"

namespace harmonia::ui
{
    /** The MIDI keyboards plugged in, each with a switch: one that's off plays nothing (a
        controller that sends junk, a second keyboard on the desk...). */
    class MidiSettingsTab final : public juce::Component
    {
    public:
        /** Where the keyboards come from and where a switch goes (the engine; tests: their own). */
        struct Source
        {
            std::function<std::vector<audio::MidiInputs::Keyboard>()> getKeyboards;
            std::function<void (const juce::String& id, bool on)> setOn;
        };

        explicit MidiSettingsTab (Source);

        /** Lists the keyboards again (one was plugged in or out, or switched). */
        void refresh();

        int getNumKeyboards() const noexcept               { return toggles.size(); }
        juce::ToggleButton* getSwitch (int index) const    { return toggles[index]; }
        juce::String getMessage() const                    { return message.getText(); }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        Source source;
        juce::Label title, message;
        juce::OwnedArray<juce::ToggleButton> toggles;

        JUCE_DECLARE_NON_COPYABLE (MidiSettingsTab)
    };
}
