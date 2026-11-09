#include "ui/settings/SettingsPanel.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        MidiSettingsTab::Source keyboardsOf (te::Engine& engine)
        {
            return { [&engine] { return audio::MidiInputs::getKeyboards (engine); },
                     [&engine] (const juce::String& id, bool on) { audio::MidiInputs::setKeyboardOn (engine, id, on); } };
        }
    }

    SettingsPanel::SettingsPanel (core::Session& s, Tab tab)
        : session (s),
          audioTab (s),
          midiTab (keyboardsOf (s.getEngine())),
          pluginTab (s)
    {
        const auto tabColour = theme::colour::background;
        tabs.addTab ("Audio", tabColour, &audioTab, false);
        tabs.addTab ("MIDI", tabColour, &midiTab, false);
        tabs.addTab ("Plugins", tabColour, &pluginTab, false);
        tabs.setTabBarDepth (theme::size::buttonHeight + 8);
        tabs.setOutline (0);
        addAndMakeVisible (tabs);

        session.getEngine().getDeviceManager().addChangeListener (this);

        showTab (tab);
        setSize (580, 500);
    }

    SettingsPanel::~SettingsPanel()
    {
        session.getEngine().getDeviceManager().removeChangeListener (this);
    }

    void SettingsPanel::showTab (Tab tab)
    {
        tabs.setCurrentTabIndex ((int) tab);
    }

    SettingsPanel::Tab SettingsPanel::getCurrentTab() const
    {
        return (Tab) juce::jlimit (0, 2, tabs.getCurrentTabIndex());
    }

    void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        midiTab.refresh();
    }

    void SettingsPanel::paint (juce::Graphics& g)
    {
        // Under the tab bar too, right of the tabs.
        g.fillAll (theme::colour::surface);
    }

    void SettingsPanel::resized()
    {
        tabs.setBounds (getLocalBounds());
    }

    void SettingsPanel::show (core::Session& session, Tab tab)
    {
        juce::DialogWindow::LaunchOptions launch;
        launch.dialogTitle = "Settings";
        launch.dialogBackgroundColour = theme::colour::background;
        launch.useNativeTitleBar = true;
        launch.resizable = false;
        launch.content.setOwned (new SettingsPanel (session, tab));
        launch.launchAsync();
    }
}
