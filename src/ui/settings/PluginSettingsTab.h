#pragma once

#include "audio/PluginScanner.h"

namespace harmonia::ui
{
    /** Plugins: the folders a scan looks in (the standard ones, and the user's own to add and
        remove), the scan itself, and whether each plugin runs in a process of its own. */
    class PluginSettingsTab final : public juce::Component,
                                    private juce::ListBoxModel
    {
    public:
        explicit PluginSettingsTab (core::Session&);
        ~PluginSettingsTab() override;

        /** The folders as listed: the standard ones first ("... (standard)"), then the user's. */
        juce::StringArray getFolderRows() const;

        /** Reads the folders from the settings again. */
        void refreshFolders();

        /** Adds a folder of one's own and scans, to find what's in it. */
        void addFolder (const juce::File&);

        juce::ListBox& getFolderList() noexcept          { return folders; }
        juce::TextButton& getRemoveButton() noexcept     { return removeButton; }
        juce::ToggleButton& getSandboxSwitch() noexcept  { return sandboxSwitch; }
        juce::String getMessage() const                  { return message.getText(); }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        core::Session& session;
        audio::PluginScanner scanner { session };

        juce::StringArray standardFolders, ownFolders;

        juce::Label foldersTitle, message, sandboxHint;
        juce::ListBox folders { "Plugin folders", this };
        juce::TextButton addButton { "Add a folder..." }, removeButton { "Remove" }, scanButton { "Scan plugins" };
        juce::ToggleButton sandboxSwitch { "Run each plugin in a process of its own (safer)" };
        std::unique_ptr<juce::FileChooser> folderChooser;

        void chooseFolder();
        void removeSelected();
        void startScan();
        void showMessage (const juce::String&, bool isProblem);
        void updateRemoveButton();

        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int) override;

        JUCE_DECLARE_NON_COPYABLE (PluginSettingsTab)
    };
}
