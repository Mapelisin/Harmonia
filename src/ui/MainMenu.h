#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui
{
    class MainComponent;

    /** The menu bar: File, Edit, View, Track, Transport, Help, every command with its shortcut
        written next to it, so everything can be found without knowing the keys. Edit works on
        what the user works in: the clips while the arrangement has the keyboard, else the notes
        in the piano roll (as Ctrl+C / Ctrl+V do). Menus are built when opened, so they always
        show the current state (what undo would take back, which panel is shown...). */
    class MainMenu final : public juce::MenuBarModel
    {
    public:
        explicit MainMenu (MainComponent&);

        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
        void menuItemSelected (int, int) override {}

        juce::PopupMenu createEditMenu();

        /** MIDI Learn, and the song's MIDI controls (each one to forget): at the end of Edit. */
        void addMidiLearnItems (juce::PopupMenu&);

        juce::PopupMenu createViewMenu();

        /** View > Plugin windows: the open ones (each to bring to the front), closing them all,
            keeping them above everything. */
        juce::PopupMenu createPluginWindowsMenu();
        juce::PopupMenu createTrackMenu();
        juce::PopupMenu createTransportMenu();
        juce::PopupMenu createHelpMenu();

        /** An item with its shortcut shown on the right. */
        static juce::PopupMenu::Item makeItem (const juce::String& text, const juce::String& keys, bool enabled,
                                               std::function<void()> action, bool ticked = false);

    private:
        MainComponent& main;
    };
}
