#include "ui/InstrumentPanel.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "core/Tracks.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int builtInSynthId = 1;
        constexpr int drumKitId = 2;
        constexpr int firstPluginId = 100;
        const juce::String builtInSynthName = "4OSC (built-in)";
        const juce::String drumKitName = "Drums (built-in)";
    }

    InstrumentPanel::InstrumentPanel (core::Session& s, te::AudioTrack& t)
        : session (s), track (t),
          crashWatcher (s, [this] (te::Plugin& plugin)
          {
              if (&plugin != audio::instruments::get (track))
                  return;

              showCurrent();

              if (audio::plugin_safety::getStatus (plugin) == audio::plugin_safety::Status::crashed)
                  showMessage (audio::plugin_safety::describeCrash (plugin), true);
          })
    {
        track.state.addListener (this);

        titleLabel.setText ("Instrument", juce::dontSendNotification);
        titleLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        titleLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));

        messageLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        messageLabel.setFont (juce::FontOptions (theme::size::smallFontSize));

        // Keyboard focus stays with the piano roll, so its shortcuts and Space keep working.
        instrumentBox.setWantsKeyboardFocus (false);
        editorButton.setWantsKeyboardFocus (false);
        scanButton.setWantsKeyboardFocus (false);
        foldersButton.setWantsKeyboardFocus (false);
        foldersButton.setTooltip ("Folders the scan looks in for plugins: add your own, such as E:\\VST");

        instrumentBox.onChange = [this] { instrumentChosen(); };
        editorButton.onClick = [this] { editorClicked(); };
        scanButton.onClick = [this] { startScan(); };
        foldersButton.onClick = [this] { showFolders(); };

        for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &instrumentBox, &editorButton,
                                                                 &scanButton, &foldersButton, &messageLabel })
            addAndMakeVisible (c);

        // An audio track plays sound files: no instrument to choose.
        if (core::tracks::isAudio (track))
        {
            titleLabel.setText ("Audio track", juce::dontSendNotification);

            for (auto* c : std::initializer_list<juce::Component*> { &instrumentBox, &editorButton, &scanButton, &foldersButton })
                c->setVisible (false);

            showMessage ("Plays sound files: drag WAV, MP3, FLAC... from Explorer onto its row, or File > Import audio.", false);
            return;
        }

        refreshList();
    }

    InstrumentPanel::~InstrumentPanel()
    {
        track.state.removeListener (this);
    }

    void InstrumentPanel::paint (juce::Graphics& g)
    {
        g.setColour (theme::colour::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::size::cornerRadius);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), theme::size::cornerRadius, 1.0f);
    }

    void InstrumentPanel::resized()
    {
        // One row: title, instrument, buttons, then the message in the remaining width.
        auto row = getLocalBounds().reduced (theme::size::padding, 0);
        row = row.withSizeKeepingCentre (row.getWidth(), theme::size::buttonHeight);

        titleLabel.setBounds (row.removeFromLeft (90));

        if (! instrumentBox.isVisible())
        {
            messageLabel.setBounds (row);
            return;
        }

        instrumentBox.setBounds (row.removeFromLeft (300));
        row.removeFromLeft (theme::size::padding);
        editorButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (theme::size::padding);
        scanButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (theme::size::padding);
        foldersButton.setBounds (row.removeFromLeft (90));
        row.removeFromLeft (theme::size::padding);
        messageLabel.setBounds (row);
    }

    void InstrumentPanel::refreshList()
    {
        available = audio::instruments::getAvailable (session);

        instrumentBox.clear (juce::dontSendNotification);
        instrumentBox.addItem (builtInSynthName, builtInSynthId);
        instrumentBox.addItem (drumKitName, drumKitId);

        if (! available.isEmpty())
            instrumentBox.addSeparator();

        for (int i = 0; i < available.size(); ++i)
            instrumentBox.addItem (available[i].name + "  (" + available[i].manufacturerName + ")", firstPluginId + i);

        showCurrent();

        if (available.isEmpty())
            showMessage ("No plugins found yet: \"Scan plugins\" looks for VST3 instruments and effects"
                         " (yours elsewhere? add their folder in \"Folders...\").", false);
    }

    void InstrumentPanel::showCurrent()
    {
        auto* current = audio::instruments::get (track);
        int selectedId = 0;

        if (dynamic_cast<te::FourOscPlugin*> (current) != nullptr)
            selectedId = builtInSynthId;
        else if (audio::instruments::playsDrumKit (track))
            selectedId = drumKitId;
        else if (auto* external = dynamic_cast<te::ExternalPlugin*> (current))
            for (int i = 0; i < available.size(); ++i)
                if (external->desc.isDuplicateOf (available[i]))
                    selectedId = firstPluginId + i;

        instrumentBox.setSelectedId (selectedId, juce::dontSendNotification);
        editorButton.setEnabled (current != nullptr);

        const auto crashed = current != nullptr && audio::plugin_safety::getStatus (*current) == audio::plugin_safety::Status::crashed;
        editorButton.setButtonText (crashed ? "Reload plugin" : "Show editor");
        if (crashed)
            editorButton.setColour (juce::TextButton::textColourOffId, theme::colour::warning);
        else
            editorButton.removeColour (juce::TextButton::textColourOffId);
    }

    void InstrumentPanel::editorClicked()
    {
        auto* current = audio::instruments::get (track);

        if (current == nullptr)
            return;

        if (audio::plugin_safety::getStatus (*current) != audio::plugin_safety::Status::crashed)
        {
            audio::instruments::showEditor (track);
            return;
        }

        if (const auto error = audio::plugin_safety::reload (*current); error.isNotEmpty())
            showMessage (current->getName() + " could not be reloaded: " + error, true);
        else
            showMessage (current->getName() + " reloaded", false);

        showCurrent();
    }

    void InstrumentPanel::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
    {
        if (child.hasType (te::IDs::PLUGIN))
            triggerAsyncUpdate();
    }

    void InstrumentPanel::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
    {
        if (child.hasType (te::IDs::PLUGIN))
            triggerAsyncUpdate();
    }

    void InstrumentPanel::handleAsyncUpdate()
    {
        // Another instrument came (an undo, a drum pattern...): the list shows it.
        if (instrumentBox.isVisible())
            showCurrent();
    }

    void InstrumentPanel::instrumentChosen()
    {
        const auto id = instrumentBox.getSelectedId();
        juce::String error;
        juce::String name;

        // A separate undo step, not merged into the last note edit.
        track.edit.getUndoManager().beginNewTransaction ("Change instrument");

        if (id == builtInSynthId)
        {
            name = builtInSynthName;
            error = audio::instruments::useBuiltInSynth (track);
        }
        else if (id == drumKitId)
        {
            name = drumKitName;
            error = audio::instruments::makeDrumTrack (track);
        }
        else if (id >= firstPluginId && id - firstPluginId < available.size())
        {
            const auto& description = available.getReference (id - firstPluginId);
            name = description.name;
            error = audio::instruments::useExternal (track, description);
        }
        else
        {
            return;
        }

        if (error.isNotEmpty())
            showMessage (error, true);
        else
            showMessage ("Instrument: " + name, false);

        refreshList();
    }

    void InstrumentPanel::startScan()
    {
        if (scanner.isScanning())
        {
            scanner.cancelScan();
            scanButton.setButtonText ("Scan plugins");
            showMessage ("Scan cancelled", false);
            return;
        }

        scanButton.setButtonText ("Stop scan");
        showMessage ("Scanning plugins...", false);

        audio::PluginScanner::Callbacks callbacks;

        callbacks.onProgress = [this] (const juce::String& file)
        {
            showMessage ("Scanning: " + juce::File (file).getFileName(), false);
        };

        callbacks.onFinished = [this] (int)
        {
            scanButton.setButtonText ("Scan plugins");
            refreshList();
            const auto effectsFound = audio::effects::getExternal (session).size();
            auto text = "Scan finished: " + juce::String (available.size()) + " instrument(s), "
                          + juce::String (effectsFound) + " effect(s) found";

            if (available.isEmpty())
                text << ". Plugins in a folder of your own: add it in \"Folders...\"";

            showMessage (text, false);
        };

        scanner.startScan (std::move (callbacks));
    }

    void InstrumentPanel::showFolders()
    {
        enum { addId = 1, firstRemoveId = 100, firstStandardId = 10000 };

        juce::PopupMenu menu;
        menu.addSectionHeader ("The scan looks in");

        // The standard folders are always searched: shown, not removable.
        if (auto* format = scanner.getFormatsToScan().getFirst())
        {
            const auto standard = format->getDefaultLocationsToSearch();

            for (int i = 0; i < standard.getNumPaths(); ++i)
                menu.addItem (firstStandardId + i, standard[i].getFullPathName() + "  (standard)", false);
        }

        const auto own = audio::PluginScanner::getOwnFolders (session);

        for (int i = 0; i < own.size(); ++i)
        {
            const auto exists = juce::File (own[i]).isDirectory();
            menu.addItem (firstRemoveId + i, "Remove " + own[i] + (exists ? juce::String() : "  (not found)"));
        }

        menu.addSeparator();
        menu.addItem (addId, "Add a folder...");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (foldersButton),
                            [safe = juce::Component::SafePointer (this), own] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            if (result == addId)
            {
                safe->chooseFolder();
            }
            else if (result >= firstRemoveId && result - firstRemoveId < own.size())
            {
                audio::PluginScanner::removeFolder (safe->session, own[result - firstRemoveId]);
                safe->showMessage ("The scan won't look in " + own[result - firstRemoveId] + " any more", false);
            }
        });
    }

    void InstrumentPanel::chooseFolder()
    {
        folderChooser = std::make_unique<juce::FileChooser> ("A folder with plugins (folders inside it are searched too)",
                                                             juce::File(), juce::String(), true);

        folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                    [safe = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;

            const auto folder = chooser.getResult();

            if (! folder.isDirectory())
                return;

            audio::PluginScanner::addFolder (safe->session, folder);

            // Finding what's there is the point of adding it.
            if (! safe->scanner.isScanning())
                safe->startScan();
        });
    }

    void InstrumentPanel::showMessage (const juce::String& text, bool isProblem)
    {
        messageLabel.setColour (juce::Label::textColourId,
                                isProblem ? theme::colour::warning : theme::colour::textSecondary);
        messageLabel.setText (text, juce::dontSendNotification);
    }
}
