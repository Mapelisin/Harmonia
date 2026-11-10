#include "ui/InstrumentPanel.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "audio/Presets.h"
#include "core/Tracks.h"
#include "ui/PluginChooser.h"
#include "ui/PresetMenu.h"
#include "ui/Theme.h"
#include "ui/settings/SettingsPanel.h"

namespace harmonia::ui
{
    namespace catalog = audio::plugin_catalog;

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
        instrumentButton.setWantsKeyboardFocus (false);
        instrumentButton.setTooltip ("The track's instrument. Click to choose another: type a few letters of its name, "
                                     "keep the ones you use as favourites.");
        editorButton.setWantsKeyboardFocus (false);
        presetsButton.setWantsKeyboardFocus (false);
        presetsButton.setTooltip ("Save the instrument's settings as a preset, or load one");
        scanButton.setWantsKeyboardFocus (false);
        foldersButton.setWantsKeyboardFocus (false);
        foldersButton.setTooltip ("Settings > Plugins: the folders the scan looks in (add your own, such as E:\\VST)");

        instrumentButton.onClick = [this] { showChooser(); };
        editorButton.onClick = [this] { editorClicked(); };
        presetsButton.onClick = [this] { showPresets(); };
        scanButton.onClick = [this] { startScan(); };
        foldersButton.onClick = [this] { SettingsPanel::show (session, SettingsPanel::Tab::plugins); };

        for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &instrumentButton, &editorButton, &presetsButton,
                                                                 &scanButton, &foldersButton, &messageLabel })
            addAndMakeVisible (c);

        // An audio track plays sound files: no instrument to choose.
        if (core::tracks::isAudio (track))
        {
            titleLabel.setText ("Audio track", juce::dontSendNotification);

            for (auto* c : std::initializer_list<juce::Component*> { &instrumentButton, &editorButton, &presetsButton, &scanButton, &foldersButton })
                c->setVisible (false);

            showMessage ("Plays sound files: drag WAV, MP3, FLAC... from Explorer onto its row, or File > Import audio.", false);
            return;
        }

        session.getEngine().getPluginManager().knownPluginList.addChangeListener (this);
        refreshList();
    }

    InstrumentPanel::~InstrumentPanel()
    {
        session.getEngine().getPluginManager().knownPluginList.removeChangeListener (this);
        track.state.removeListener (this);
    }

    void InstrumentPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        refreshList();
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

        if (! instrumentButton.isVisible())
        {
            messageLabel.setBounds (row);
            return;
        }

        instrumentButton.setBounds (row.removeFromLeft (300));
        row.removeFromLeft (theme::size::padding);
        editorButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (theme::size::padding);
        presetsButton.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (theme::size::padding);
        scanButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (theme::size::padding);
        foldersButton.setBounds (row.removeFromLeft (90));
        row.removeFromLeft (theme::size::padding);
        messageLabel.setBounds (row);
    }

    void InstrumentPanel::refreshList()
    {
        showCurrent();

        if (audio::instruments::getAvailable (session).isEmpty())
            showMessage ("No plugins found yet: \"Scan plugins\" looks for VST3 instruments and effects"
                         " (yours elsewhere? add their folder in \"Folders...\").", false);
    }

    void InstrumentPanel::showCurrent()
    {
        auto* current = audio::instruments::get (track);

        // As the chooser lists it; a plugin the scan no longer finds: by its own name.
        const auto entry = current != nullptr ? catalog::find (session, *current) : std::nullopt;
        instrumentButton.setButtonText (entry.has_value() ? entry->getLabel()
                                        : current != nullptr ? current->getName()
                                                             : juce::String ("Choose an instrument..."));
        editorButton.setEnabled (current != nullptr);
        presetsButton.setEnabled (current != nullptr && audio::presets::canHavePresets (*current));

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

    void InstrumentPanel::showPresets()
    {
        auto* current = audio::instruments::get (track);

        if (current == nullptr)
            return;

        preset_menu::make (*current, [safe = juce::Component::SafePointer<InstrumentPanel> (this)] (const juce::String& text, bool isProblem)
        {
            if (safe != nullptr)
                safe->showMessage (text, isProblem);
        }).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton));
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
        if (instrumentButton.isVisible())
            showCurrent();
    }

    void InstrumentPanel::showChooser()
    {
        PluginChooser::show (session, catalog::Kind::instrument, instrumentButton,
                             [safe = juce::Component::SafePointer<InstrumentPanel> (this)] (const catalog::Entry& entry)
        {
            if (safe != nullptr)
                safe->choose (entry);
        });
    }

    void InstrumentPanel::choose (const catalog::Entry& entry)
    {
        // A separate undo step, not merged into the last note edit.
        track.edit.getUndoManager().beginNewTransaction ("Change instrument");
        const auto error = catalog::useInstrument (session, track, entry);

        if (error.isNotEmpty())
        {
            // Nothing changed, so there is nothing to undo either (trying a plugin leaves traces
            // in the step, none of them in the song).
            track.edit.getUndoManager().undoCurrentTransactionOnly();
            showMessage (error, true);
        }
        else if (audio::instruments::playsSampler (track))
        {
            // It plays nothing until it has a sound: its window, where the sound goes, at once.
            showMessage ("Instrument: " + entry.getLabel() + ". Drop a sound on its window, or on the piano roll's keys.", false);
            audio::instruments::showEditor (track);
        }
        else
        {
            showMessage ("Instrument: " + entry.getLabel(), false);
        }

        showCurrent();
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

        audio::PluginScanner::Callbacks callbacks;

        callbacks.onProgress = [this] (const juce::String& file)
        {
            showMessage ("Scanning: " + juce::File (file).getFileName(), false);
        };

        callbacks.onFinished = [this] (int)
        {
            scanButton.setButtonText ("Scan plugins");
            refreshList();
            const auto instrumentsFound = audio::instruments::getAvailable (session).size();
            const auto effectsFound = audio::effects::getExternal (session).size();
            auto text = "Scan finished: " + juce::String (instrumentsFound) + " instrument(s), "
                          + juce::String (effectsFound) + " effect(s) found";

            if (instrumentsFound == 0)
                text << ". Plugins in a folder of your own: add it in \"Folders...\"";

            showMessage (text, false);
        };

        if (! scanner.startScan (std::move (callbacks)))
        {
            showMessage ("A scan is running already (in the settings): wait for it to finish.", true);
            return;
        }

        scanButton.setButtonText ("Stop scan");
        showMessage ("Scanning plugins...", false);
    }

    void InstrumentPanel::showMessage (const juce::String& text, bool isProblem)
    {
        messageLabel.setColour (juce::Label::textColourId,
                                isProblem ? theme::colour::warning : theme::colour::textSecondary);
        messageLabel.setText (text, juce::dontSendNotification);
    }
}
