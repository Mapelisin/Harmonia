#include "ui/settings/PluginSettingsTab.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        void styleSmall (juce::Label& label)
        {
            label.setFont (juce::FontOptions (theme::size::smallFontSize));
            label.setColour (juce::Label::textColourId, theme::colour::textSecondary);
            label.setJustificationType (juce::Justification::topLeft);
            label.setMinimumHorizontalScale (1.0f);
        }
    }

    PluginSettingsTab::PluginSettingsTab (core::Session& s)
        : session (s)
    {
        foldersTitle.setText ("Folders the scan looks in (and every folder inside them)", juce::dontSendNotification);
        foldersTitle.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        foldersTitle.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        styleSmall (message);
        styleSmall (sandboxHint);

        folders.setRowHeight (theme::size::buttonHeight);
        folders.setColour (juce::ListBox::backgroundColourId, theme::colour::surface);
        folders.setColour (juce::ListBox::outlineColourId, theme::colour::border);
        folders.setOutlineThickness (1);

        addButton.onClick = [this] { chooseFolder(); };
        removeButton.onClick = [this] { removeSelected(); };
        scanButton.onClick = [this] { startScan(); };

        sandboxSwitch.setToggleState (session.isPluginSandboxOn(), juce::dontSendNotification);
        sandboxSwitch.setEnabled (session.getPluginSandbox() != nullptr);
        sandboxSwitch.onClick = [this]
        {
            const auto on = sandboxSwitch.getToggleState();
            session.setPluginSandboxOn (on);
            showMessage (on ? "Plugins loaded from now on run in processes of their own. Reopen the project for all of them."
                            : "Plugins loaded from now on load into Harmonia: a crash in one closes Harmonia. "
                              "Reopen the project for all of them.",
                         ! on);
        };
        sandboxHint.setText ("If a plugin crashes or hangs, only it goes silent: the song keeps playing and the plugin can be "
                             "reloaded. Turn it off only for a plugin that doesn't work this way.",
                             juce::dontSendNotification);

        for (auto* c : std::initializer_list<juce::Component*> { &foldersTitle, &folders, &addButton, &removeButton, &scanButton,
                                                                 &message, &sandboxSwitch, &sandboxHint })
            addAndMakeVisible (c);

        if (auto* format = scanner.getFormatsToScan().getFirst())
        {
            const auto standard = format->getDefaultLocationsToSearch();

            for (int i = 0; i < standard.getNumPaths(); ++i)
                standardFolders.add (standard[i].getFullPathName());
        }

        refreshFolders();

        if (audio::PluginScanner::isAnyScanRunning())
            showMessage ("A scan is running (in the instrument panel).", false);
    }

    PluginSettingsTab::~PluginSettingsTab()
    {
        scanner.cancelScan();
    }

    juce::StringArray PluginSettingsTab::getFolderRows() const
    {
        juce::StringArray rows;

        for (const auto& folder : standardFolders)
            rows.add (folder + "  (standard)");

        for (const auto& folder : ownFolders)
            rows.add (juce::File (folder).isDirectory() ? folder : folder + "  (not found)");

        return rows;
    }

    void PluginSettingsTab::refreshFolders()
    {
        ownFolders = audio::PluginScanner::getOwnFolders (session);
        folders.updateContent();
        folders.repaint();
        updateRemoveButton();
    }

    void PluginSettingsTab::addFolder (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return;

        audio::PluginScanner::addFolder (session, folder);
        refreshFolders();

        // Finding what's there is the point of adding it.
        if (! scanner.isScanning())
            startScan();
    }

    void PluginSettingsTab::chooseFolder()
    {
        folderChooser = std::make_unique<juce::FileChooser> ("A folder with plugins (folders inside it are searched too)",
                                                             juce::File(), juce::String(), true);

        folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                    [safe = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
        {
            if (safe != nullptr)
                safe->addFolder (chooser.getResult());
        });
    }

    void PluginSettingsTab::removeSelected()
    {
        const auto index = folders.getSelectedRow() - standardFolders.size();

        if (! juce::isPositiveAndBelow (index, ownFolders.size()))
            return;

        const auto path = ownFolders[index];
        audio::PluginScanner::removeFolder (session, path);
        folders.deselectAllRows();
        refreshFolders();
        showMessage ("The scan won't look in " + path + " any more. Plugins found there stay in the list.", false);
    }

    void PluginSettingsTab::startScan()
    {
        if (scanner.isScanning())
        {
            scanner.cancelScan();
            scanButton.setButtonText ("Scan plugins");
            showMessage ("Scan cancelled", false);
            return;
        }

        audio::PluginScanner::Callbacks callbacks;
        callbacks.onProgress = [this] (const juce::String& file)
        {
            showMessage ("Scanning: " + juce::File (file).getFileName(), false);
        };
        callbacks.onFinished = [this] (int)
        {
            scanButton.setButtonText ("Scan plugins");
            showMessage ("Scan finished: " + juce::String (audio::instruments::getAvailable (session).size()) + " instrument(s), "
                             + juce::String (audio::effects::getExternal (session).size()) + " effect(s) found",
                         false);
        };

        if (! scanner.startScan (std::move (callbacks)))
        {
            showMessage ("A scan is running already (in the instrument panel): wait for it to finish.", true);
            return;
        }

        scanButton.setButtonText ("Stop scan");
        showMessage ("Scanning plugins...", false);
    }

    void PluginSettingsTab::showMessage (const juce::String& text, bool isProblem)
    {
        message.setColour (juce::Label::textColourId, isProblem ? theme::colour::warning : theme::colour::textSecondary);
        message.setText (text, juce::dontSendNotification);
    }

    void PluginSettingsTab::updateRemoveButton()
    {
        removeButton.setEnabled (juce::isPositiveAndBelow (folders.getSelectedRow() - standardFolders.size(), ownFolders.size()));
    }

    int PluginSettingsTab::getNumRows()
    {
        return standardFolders.size() + ownFolders.size();
    }

    void PluginSettingsTab::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        const auto rows = getFolderRows();

        if (! juce::isPositiveAndBelow (row, rows.size()))
            return;

        // The standard folders are always searched: shown, but not to be removed.
        const auto standard = row < standardFolders.size();

        if (selected && ! standard)
            g.fillAll (theme::colour::accent.withAlpha (0.25f));

        g.setColour (standard ? theme::colour::textSecondary : theme::colour::textPrimary);
        g.setFont (juce::FontOptions (theme::size::fontSize));
        g.drawText (rows[row], 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void PluginSettingsTab::selectedRowsChanged (int)
    {
        updateRemoveButton();
    }

    void PluginSettingsTab::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
    }

    void PluginSettingsTab::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding * 2);
        foldersTitle.setBounds (area.removeFromTop (theme::size::buttonHeight));
        area.removeFromTop (theme::size::padding / 2);

        auto sandbox = area.removeFromBottom (theme::size::buttonHeight + 36);
        sandboxSwitch.setBounds (sandbox.removeFromTop (theme::size::buttonHeight));
        sandboxHint.setBounds (sandbox);
        area.removeFromBottom (theme::size::padding * 2);

        message.setBounds (area.removeFromBottom (34));
        area.removeFromBottom (theme::size::padding / 2);

        auto buttons = area.removeFromBottom (theme::size::buttonHeight);
        addButton.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (theme::size::padding);
        removeButton.setBounds (buttons.removeFromLeft (90));
        scanButton.setBounds (buttons.removeFromRight (110));
        area.removeFromBottom (theme::size::padding);

        folders.setBounds (area);
    }
}
