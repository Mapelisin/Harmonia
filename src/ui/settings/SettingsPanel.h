#pragma once

#include "ui/settings/AudioSettingsTab.h"
#include "ui/settings/MidiSettingsTab.h"
#include "ui/settings/PluginSettingsTab.h"

namespace harmonia::ui
{
    /** The settings window's content: tabs for the sound card, the MIDI keyboards and the plugins.
        Every change applies at once and is kept for the next start. */
    class SettingsPanel final : public juce::Component,
                                private juce::ChangeListener
    {
    public:
        enum class Tab { audio, midi, plugins };

        SettingsPanel (core::Session&, Tab);
        ~SettingsPanel() override;

        void showTab (Tab);
        Tab getCurrentTab() const;

        AudioSettingsTab& getAudioTab() noexcept      { return audioTab; }
        MidiSettingsTab& getMidiTab() noexcept        { return midiTab; }
        PluginSettingsTab& getPluginTab() noexcept    { return pluginTab; }

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens the settings in a window of their own at this tab (File > Settings, Ctrl+,). */
        static void show (core::Session&, Tab = Tab::audio);

    private:
        core::Session& session;
        AudioSettingsTab audioTab;
        MidiSettingsTab midiTab;
        PluginSettingsTab pluginTab;

        // Shows the tabs above without owning them; declared after them, so it goes first.
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

        // A keyboard plugged in or out, or switched.
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        JUCE_DECLARE_NON_COPYABLE (SettingsPanel)
    };
}
