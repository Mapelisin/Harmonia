#include "ui/MainComponent.h"
#include "core/Markers.h"
#include "core/MidiFiles.h"
#include "core/Tracks.h"
#include "audio/DeviceStatus.h"
#include "audio/Loudness.h"
#include "audio/MixCheck.h"
#include "ui/ComputerKeyboard.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int trackRowHeight = 40;
        constexpr int appNameWidth = 112, menuBarWidth = 380, cpuMeterWidth = 132, tabWidth = 86;
        constexpr int lowerPanelTabs = 1001;
    }

    MainComponent::MainComponent (core::Session& s, core::Project& p)
        : session (s), project (p),
          cpuMeter ([&s]() -> std::optional<float>
          {
              if (! audio::getDeviceStatus (s).isOpen)
                  return std::nullopt;

              return audio::getAudioCpuLoad (s);
          })
    {
        // Clicks on widgets that don't take focus land here, so Space and the editing
        // shortcuts keep working wherever the user clicked.
        setWantsKeyboardFocus (true);

        pianoRollModel.setAuditioner (&auditioner);
        pianoRollModel.addListener (this);
        pianoRoll.onMessage = [this] (const juce::String& text) { showMessage (text); };
        arrangementModel.addListener (this);
        arrangementModel.onCurrentChanged = [this] { currentChanged(); };

        midiInputs.onMessage = [this] (const juce::MidiMessage& m, double wallMs) { noteInput.handleMidiMessage (m, wallMs); };
        midiInputs.onDevicesChanged = [this] { updateMidiStatus(); };
        noteInput.onStateChanged = [this]
        {
            pianoRoll.setLetterShortcutsEnabled (! noteInput.getComputerKeyboard().isEnabled());
            arrangement.setLetterShortcutsEnabled (! noteInput.getComputerKeyboard().isEnabled());
            transportBar.refresh();
        };
        updateMidiStatus();

        transportBar.onPlayModeToggled = [this] { togglePlayMode(); };
        transportBar.onGoToStart = [this] { goToStart(); };
        arrangement.onPositionChosen = [this] (double beat) { playFrom (beat); };

        trackNameLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        trackNameLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);

        shortcutsButton.setTooltip ("Keyboard shortcuts and mouse tricks, with a search (F1)");
        shortcutsButton.onClick = [this] { showShortcuts (! areShortcutsShown()); };
        shortcuts.onClose = [this] { showShortcuts (false); };

        // The lower part shows the sounds, the piano roll, the automation or the mixer (F6 / F7 / F8 / F9).
        soundsTab.setTooltip ("Sounds: your folders of samples and loops; listen, then drag them onto the tracks (F6)");
        pianoRollTab.setTooltip ("Piano roll of the open clip (F7)");
        automationTab.setTooltip ("Automation: lines that change volume, pan, sends or effects over the song (F8)");
        mixerTab.setTooltip ("Mixer: effects, volume, pan (F9)");

        for (auto* tab : { &soundsTab, &pianoRollTab, &automationTab, &mixerTab })
        {
            tab->setClickingTogglesState (true);
            tab->setRadioGroupId (lowerPanelTabs);
            tab->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.35f));
        }

        pianoRollTab.setToggleState (true, juce::dontSendNotification);
        pianoRollTab.onClick = [this] { showLowerPanel (LowerPanel::pianoRoll); };
        automationTab.onClick = [this] { showLowerPanel (LowerPanel::automation); };
        mixerTab.onClick = [this] { showLowerPanel (LowerPanel::mixer); };
        soundsTab.onClick = [this] { showLowerPanel (LowerPanel::sounds); };
        soundBrowser.onAddToSong = [this] (const juce::File& file) { addFilesAtPlayhead ({ file }); };

        // The automation lane scrolls and zooms with the arrangement.
        arrangement.onViewChanged = [this] { automationPanel.getLane().repaint(); };
        automationPanel.setMargins (arrangement::Arrangement::headerWidth, arrangement::Arrangement::scrollBarSize);

        mixer.onMessage = [this] (const juce::String& text, bool isProblem)
        {
            if (isProblem)
                statusBar.setWarning (text);
            else
                statusBar.setMessage (text);
        };

        // Record (R) + Play records the armed audio tracks too.
        transport.recordsAudio = [this] { return noteInput.isRecordArmed() && audioInputs.anyArmed(); };

        arrangement.onMessage = [this] (const juce::String& text, bool isProblem)
        {
            if (isProblem)
                statusBar.setWarning (text);
            else
                statusBar.setMessage (text);
        };

        for (auto* c : std::initializer_list<juce::Component*> { &shortcutsButton, &soundsTab, &pianoRollTab, &automationTab, &mixerTab })
            c->setWantsKeyboardFocus (false);

        // Arrangement above, piano roll below; the bar between them can be dragged.
        splitLayout.setItemLayout (0, 110.0, -1.0, 250.0);
        splitLayout.setItemLayout (1, 6.0, 6.0, 6.0);
        splitLayout.setItemLayout (2, 200.0, -1.0, -0.62);

        for (auto* c : std::initializer_list<juce::Component*> { &menuBar, &cpuMeter, &transportBar, &shortcutsButton, &trackNameLabel,
                                                                 &soundsTab, &pianoRollTab, &automationTab, &mixerTab, &arrangement, &splitter, &pianoRoll,
                                                                 &statusBar })
            addAndMakeVisible (c);

        addChildComponent (mixer);
        addChildComponent (automationPanel);
        addChildComponent (soundBrowser);
        addChildComponent (shortcuts);     // over everything

        currentChanged();
        setSize (1280, 800);

        // Keyboard focus to the piano roll once the window is on screen.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&pianoRoll)]
        {
            if (safe != nullptr && safe->isShowing())
                safe->grabKeyboardFocus();
        });
    }

    MainComponent::~MainComponent()
    {
        arrangementModel.onCurrentChanged = nullptr;
        arrangement.onViewChanged = nullptr;
        arrangementModel.removeListener (this);
        pianoRollModel.removeListener (this);
        pianoRollModel.setAuditioner (nullptr);
        transportBar.onPlayModeToggled = nullptr;
        transportBar.onGoToStart = nullptr;
        menuBar.setModel (nullptr);
        transport.recordsAudio = nullptr;
        noteInput.onStateChanged = nullptr;
        midiInputs.onMessage = nullptr;
        midiInputs.onDevicesChanged = nullptr;
        transport.stop();
        noteInput.finishTake();
        notePreview.setTrack (nullptr);
    }

    void MainComponent::setFileCommands (FileCommands* commands)
    {
        fileCommands = commands;
        repaint();
    }

    //==============================================================================
    void MainComponent::currentChanged()
    {
        // A take belongs to the clip it was recorded into.
        noteInput.finishTake();

        auto* track = arrangementModel.getCurrentTrack();
        auto* clip = arrangementModel.getOpenClip();

        if (track != currentTrack.get())
        {
            currentTrack = track;
            notePreview.setTrack (track);
            midiInputs.setTrack (track);
            instrumentPanel.reset();

            if (track != nullptr)
            {
                instrumentPanel = std::make_unique<InstrumentPanel> (session, *track);
                addAndMakeVisible (*instrumentPanel);
            }

            resized();
        }

        trackNameLabel.setText (track != nullptr ? track->getName() : juce::String ("No track"), juce::dontSendNotification);
        pianoRoll.getGrid().setEmptyText (track != nullptr && core::tracks::isAudio (*track)
                                              ? "Audio track: its clips are sound files. The piano roll edits the MIDI clips of instrument tracks."
                                              : pianoroll::NoteGrid::getDefaultEmptyText());

        if (clip != pianoRollModel.getClip())
        {
            pianoRollModel.setClip (clip);
            pianoRoll.showClip();
        }

        automationPanel.refresh();
        updateLoop();
    }

    void MainComponent::updateLoop()
    {
        // Clip mode loops the open clip like a pattern; song mode the loop region or the song.
        const auto range = arrangementModel.getPlayRange();

        if (std::abs (range.start - playedLoop.start) > 1.0e-9 || std::abs (range.end - playedLoop.end) > 1.0e-9)
        {
            playedLoop = range;
            transport.setLoopBeats (range.start, range.end);
        }
    }

    void MainComponent::setPlayMode (arrangement::PlayMode mode)
    {
        arrangementModel.setPlayMode (mode);
        updateLoop();
        transportBar.refresh();
        arrangement.repaint();
    }

    void MainComponent::togglePlayMode()
    {
        using arrangement::PlayMode;
        setPlayMode (arrangementModel.getPlayMode() == PlayMode::clip ? PlayMode::song : PlayMode::clip);
    }

    void MainComponent::goToStart()
    {
        // Song: its very start (not the loop region's: Home means the beginning). Clip: the clip's start.
        const auto clipMode = arrangementModel.getPlayMode() == arrangement::PlayMode::clip;
        const auto beat = clipMode ? std::max (0.0, arrangementModel.getPlayRange().start) : 0.0;
        transport.setPositionSeconds (project.getEdit().tempoSequence.toTime (te::BeatPosition::fromBeats (beat)).inSeconds());
        transportBar.refresh();
    }

    void MainComponent::jumpToMarker (bool next)
    {
        const auto now = project.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getPositionSeconds())).inBeats();
        const auto target = next ? core::markers::findAfter (project.getEdit(), now + 1.0e-6)
                                 : core::markers::findAtOrBefore (project.getEdit(), now - 1.0e-3);

        if (target.has_value())
            playFrom (target->beat);
        else if (! next)
            playFrom (0.0);
    }

    void MainComponent::undo()
    {
        pianoRollModel.undo();
    }

    void MainComponent::redo()
    {
        pianoRollModel.redo();
    }

    bool MainComponent::isArrangementTheEditTarget() const
    {
        return arrangement.hasKeyboardFocus (true) || lowerPanel != LowerPanel::pianoRoll;
    }

    void MainComponent::playFrom (double beat)
    {
        // Clicking the arrangement's ruler means the song, as in FL Studio.
        arrangementModel.setPlayMode (arrangement::PlayMode::song);
        updateLoop();
        transport.setPositionSeconds (project.getEdit().tempoSequence.toTime (te::BeatPosition::fromBeats (beat)).inSeconds());
        transportBar.refresh();
    }

    void MainComponent::updateMidiStatus()
    {
        const auto names = midiInputs.getDeviceNames();
        statusBar.setMidiText (names.isEmpty() ? juce::String ("No MIDI keyboard")
                                               : "MIDI: " + names.joinIntoString (", "));
    }

    void MainComponent::pianoRollChanged()
    {
        // Notes drawn past the end make the clip longer.
        updateLoop();
    }

    void MainComponent::arrangementChanged()
    {
        if (auto* track = arrangementModel.getCurrentTrack())
            trackNameLabel.setText (track->getName(), juce::dontSendNotification);

        updateLoop();
        transportBar.refresh();
    }

    //==============================================================================
    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        // The menu row and the transport row under it, one panel.
        auto top = getLocalBounds().removeFromTop (theme::size::menuBarHeight + theme::size::headerHeight);
        g.setColour (theme::colour::surface);
        g.fillRect (top);
        g.setColour (theme::colour::border);
        g.fillRect (top.removeFromBottom (1));
        g.setColour (theme::colour::border.withAlpha (0.5f));
        g.fillRect (top.removeFromTop (theme::size::menuBarHeight).removeFromBottom (1).reduced (theme::size::padding, 0));

        // Harmonia's mark and name.
        auto name = getLocalBounds().removeFromTop (theme::size::menuBarHeight).reduced (theme::size::padding, 0).removeFromLeft (appNameWidth);
        icons::drawLogo (g, name.removeFromLeft (18).toFloat().withSizeKeepingCentre (18.0f, 18.0f));
        name.removeFromLeft (7);
        g.setColour (theme::colour::textPrimary);
        g.setFont (juce::FontOptions (theme::size::fontSize + 1.0f, juce::Font::bold));
        g.drawText ("Harmonia", name, juce::Justification::centredLeft);

        // The track row: the current track's name over the headers, its instrument to the right.
        auto trackRow = getLocalBounds().withTrimmedTop (theme::size::menuBarHeight + theme::size::headerHeight).removeFromTop (trackRowHeight);
        g.setColour (theme::colour::border);
        g.fillRect (trackRow.removeFromBottom (1));
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        auto menuRow = area.removeFromTop (theme::size::menuBarHeight).reduced (theme::size::padding, 0);
        auto header = area.removeFromTop (theme::size::headerHeight).reduced (theme::size::padding, 0);
        statusBar.setBounds (area.removeFromBottom (theme::size::statusBarHeight));
        shortcuts.setBounds (getLocalBounds());

        // Menu row: name, menus; on the right the audio load and the shortcuts.
        menuRow.removeFromLeft (appNameWidth);
        shortcutsButton.setBounds (menuRow.removeFromRight (26).withSizeKeepingCentre (26, 20));
        menuRow.removeFromRight (theme::size::padding);
        cpuMeter.setBounds (menuRow.removeFromRight (cpuMeterWidth).reduced (0, 3));
        menuBar.setBounds (menuRow.removeFromLeft (std::min (menuBarWidth, menuRow.getWidth())));

        // The transport in the middle of its row.
        const auto transportWidth = std::min (TransportBar::getPreferredWidth(), header.getWidth());
        transportBar.setBounds (header.withSizeKeepingCentre (transportWidth, header.getHeight()));

        auto trackRow = area.removeFromTop (trackRowHeight);
        trackNameLabel.setBounds (trackRow.removeFromLeft (arrangement::Arrangement::headerWidth).reduced (theme::size::padding / 2, 0));

        auto tabs = trackRow.removeFromRight (4 * tabWidth + theme::size::padding).withTrimmedRight (theme::size::padding);
        mixerTab.setBounds (tabs.removeFromRight (tabWidth).withSizeKeepingCentre (tabWidth, theme::size::buttonHeight));
        automationTab.setBounds (tabs.removeFromRight (tabWidth).withSizeKeepingCentre (tabWidth, theme::size::buttonHeight));
        pianoRollTab.setBounds (tabs.removeFromRight (tabWidth).withSizeKeepingCentre (tabWidth, theme::size::buttonHeight));
        soundsTab.setBounds (tabs.removeFromRight (tabWidth).withSizeKeepingCentre (tabWidth, theme::size::buttonHeight));

        if (instrumentPanel != nullptr)
            instrumentPanel->setBounds (trackRow.reduced (theme::size::padding / 2, 4));

        juce::Component* parts[] = { &arrangement, &splitter, &pianoRoll };
        splitLayout.layOutComponents (parts, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
        mixer.setBounds (pianoRoll.getBounds());
        automationPanel.setBounds (pianoRoll.getBounds());
        soundBrowser.setBounds (pianoRoll.getBounds());
    }

    void MainComponent::showLowerPanel (LowerPanel panel)
    {
        const auto hadFocus = pianoRoll.hasKeyboardFocus (true) || mixer.hasKeyboardFocus (true)
                           || automationPanel.hasKeyboardFocus (true) || soundBrowser.hasKeyboardFocus (true);

        lowerPanel = panel;
        pianoRollTab.setToggleState (panel == LowerPanel::pianoRoll, juce::dontSendNotification);
        automationTab.setToggleState (panel == LowerPanel::automation, juce::dontSendNotification);
        mixerTab.setToggleState (panel == LowerPanel::mixer, juce::dontSendNotification);
        soundsTab.setToggleState (panel == LowerPanel::sounds, juce::dontSendNotification);
        pianoRoll.setVisible (panel == LowerPanel::pianoRoll);
        automationPanel.setVisible (panel == LowerPanel::automation);
        mixer.setVisible (panel == LowerPanel::mixer);
        soundBrowser.setVisible (panel == LowerPanel::sounds);

        if (panel != LowerPanel::sounds)
            filePreview.stop();

        if (panel == LowerPanel::automation)
            automationPanel.refresh();

        // Keys keep working: the piano roll takes them back, or this component does.
        if (panel == LowerPanel::pianoRoll && (hadFocus || ! hasKeyboardFocus (true)) && pianoRoll.isShowing())
            pianoRoll.grabKeyboardFocus();
        else if (panel == LowerPanel::sounds && soundBrowser.isShowing())
            soundBrowser.getList().grabKeyboardFocus();     // Up / Down go through the sounds
        else if (panel != LowerPanel::pianoRoll && hadFocus && isShowing())
            grabKeyboardFocus();
    }

    void MainComponent::showShortcuts (bool shouldShow)
    {
        if (shouldShow == shortcuts.isVisible())
            return;

        if (shouldShow)
        {
            shortcuts.setFilter ({});
            shortcuts.toFront (false);
            shortcuts.setVisible (true);
        }
        else
        {
            shortcuts.setVisible (false);

            // Keys go back to the piano roll (or to here).
            if (lowerPanel == LowerPanel::pianoRoll && pianoRoll.isShowing())
                pianoRoll.grabKeyboardFocus();
            else if (isShowing())
                grabKeyboardFocus();
        }
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::F1Key)
        {
            showShortcuts (! areShortcutsShown());
            return true;
        }

        if (key == juce::KeyPress::spaceKey)
        {
            transport.togglePlayStop();
            return true;
        }

        auto& keyboard = noteInput.getComputerKeyboard();
        const auto mods = key.getModifiers();
        const auto letter = keys::getLetter (key);
        const auto code = key.getKeyCode();

        // Ctrl+Left / Ctrl+Right: play from the previous / next marker.
        if (mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown()
            && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        {
            jumpToMarker (code == juce::KeyPress::rightKey);
            return true;
        }

        if (code == juce::KeyPress::homeKey && ! mods.isAnyModifierKeyDown())
        {
            goToStart();
            return true;
        }

        if (! mods.isAnyModifierKeyDown()
            && (code == juce::KeyPress::F6Key || code == juce::KeyPress::F7Key || code == juce::KeyPress::F8Key
                || code == juce::KeyPress::F9Key))
        {
            showLowerPanel (code == juce::KeyPress::F6Key   ? LowerPanel::sounds
                            : code == juce::KeyPress::F7Key ? LowerPanel::pianoRoll
                            : code == juce::KeyPress::F8Key ? LowerPanel::automation
                                                            : LowerPanel::mixer);
            return true;
        }

        if (mods.isCommandDown() && ! mods.isAltDown())
        {
            const auto shift = mods.isShiftDown();

            switch (letter)
            {
                case 'N': if (fileCommands != nullptr && ! shift) fileCommands->newProject(); return ! shift;
                case 'O': if (fileCommands != nullptr && ! shift) fileCommands->openProject(); return ! shift;
                case 'S':
                    if (fileCommands != nullptr)
                    {
                        if (shift) fileCommands->saveProjectAs();
                        else       fileCommands->saveProject();
                    }
                    return true;
                case 'R': if (fileCommands != nullptr && ! shift) fileCommands->exportWav(); return ! shift;
                case 'T': if (! shift) keyboard.setEnabled (! keyboard.isEnabled()); return ! shift;
                case 'M': if (! shift) toggleMetronome(); return ! shift;
                default:  break;
            }
        }

        if (keyboard.isEnabled())
        {
            if (! mods.isAnyModifierKeyDown() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
            {
                keyboard.setOctave (keyboard.getOctave() + (code == juce::KeyPress::upKey ? 1 : -1));
                return true;
            }

            // Letters, digits and punctuation are piano keys now (played in keyStateChanged),
            // so they must not trigger the one-letter shortcuts.
            if (keys::isTypingKey (key))
                return true;
        }
        else if (! mods.isAnyModifierKeyDown())
        {
            if (letter == 'R')
            {
                noteInput.setRecordArmed (! noteInput.isRecordArmed());
                return true;
            }

            if (letter == 'L')
            {
                togglePlayMode();
                return true;
            }
        }

        // Undo and redo work wherever the focus is.
        if (mods.isCommandDown() && (letter == 'Z' || letter == 'Y'))
            return pianoRoll.keyPressed (key);

        // The arrangement had its go already (it's focused): its keys don't edit notes.
        // Nor do they while the piano roll is hidden behind the mixer or the automation.
        if (arrangement.hasKeyboardFocus (true) || lowerPanel != LowerPanel::pianoRoll)
            return false;

        // Editing shortcuts work even when the piano roll itself doesn't have focus.
        return pianoRoll.keyPressed (key);
    }

    bool MainComponent::keyStateChanged (bool)
    {
        auto& keyboard = noteInput.getComputerKeyboard();

        if (! keyboard.isEnabled())
            return false;

        // With Ctrl or Alt held the keys are shortcuts (Ctrl+Z...), not notes.
        const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
        keyboard.update (! (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown()));
        return false;
    }

    void MainComponent::toggleMetronome()
    {
        auto& edit = project.getEdit();
        edit.clickTrackEnabled = ! edit.clickTrackEnabled.get();
        transportBar.refresh();
    }

    void MainComponent::showWarning (const juce::String& text)
    {
        statusBar.setWarning (text);
    }

    void MainComponent::showMessage (const juce::String& text)
    {
        statusBar.setMessage (text);
    }

    void MainComponent::pluginStatusChanged (te::Plugin& plugin)
    {
        if (&plugin.edit != &project.getEdit())
            return;

        if (audio::plugin_safety::getStatus (plugin) == audio::plugin_safety::Status::crashed)
            showWarning (audio::plugin_safety::describeCrash (plugin));
        else
            showWarning ({});

        mixer.refresh();
    }

    //==============================================================================
    juce::PopupMenu MainComponent::createFileMenu()
    {
        const auto hasCommands = fileCommands != nullptr;
        juce::Component::SafePointer<MainComponent> safe (this);

        const auto run = [safe] (void (FileCommands::* command)())
        {
            return [safe, command]
            {
                if (safe != nullptr && safe->fileCommands != nullptr)
                    (safe->fileCommands->*command)();
            };
        };

        juce::PopupMenu menu;
        menu.addItem ("New project  (Ctrl+N)", hasCommands, false, run (&FileCommands::newProject));
        menu.addItem ("Open...  (Ctrl+O)", hasCommands, false, run (&FileCommands::openProject));

        // The last projects: name, and where it is.
        juce::PopupMenu recentMenu;
        const auto recent = hasCommands ? fileCommands->getRecentProjects() : juce::Array<juce::File>();

        for (const auto& file : recent)
        {
            juce::PopupMenu::Item item (file.getFileNameWithoutExtension());
            item.shortcutKeyDescription = file.getParentDirectory().getFullPathName();
            item.action = [safe, file] { if (safe != nullptr && safe->fileCommands != nullptr) safe->fileCommands->openRecentProject (file); };
            recentMenu.addItem (std::move (item));
        }

        if (! recent.isEmpty())
        {
            recentMenu.addSeparator();
            recentMenu.addItem ("Clear the list", run (&FileCommands::clearRecentProjects));
        }

        menu.addSubMenu ("Open recent", recentMenu, ! recent.isEmpty());
        menu.addSeparator();
        menu.addItem ("Save  (Ctrl+S)", hasCommands, false, run (&FileCommands::saveProject));
        menu.addItem ("Save as...  (Ctrl+Shift+S)", hasCommands, false, run (&FileCommands::saveProjectAs));
        menu.addSeparator();
        menu.addItem ("Import audio or MIDI...", [safe] { if (safe != nullptr) safe->chooseFilesToImport(); });
        menu.addItem ("Export WAV...  (Ctrl+R)", hasCommands, false, run (&FileCommands::exportWav));
        menu.addItem ("Export stems (WAV per track)...", hasCommands, false, run (&FileCommands::exportStems));

        const auto loop = arrangementModel.getLoopRegion();
        menu.addItem (loop.has_value() ? "Export the loop region as WAV (" + describeBars (*loop, arrangementModel.getBeatsPerBar()) + ")..."
                                       : juce::String ("Export the loop region as WAV (Ctrl+drag on the ruler first)"),
                      hasCommands && loop.has_value(), false, run (&FileCommands::exportLoopWav));
        menu.addItem ("Export MIDI...", [safe] { if (safe != nullptr) safe->chooseMidiExport(); });
        menu.addSeparator();
        menu.addItem ("Check loudness for streaming...", [safe] { if (safe != nullptr) safe->checkLoudness(); });
        menu.addItem ("Check the mix (Mix Doctor)...", [safe] { if (safe != nullptr) safe->checkMix(); });
        menu.addSeparator();
        menu.addItem ("Audio settings...", session.devicesEnabled(), false, [safe] { if (safe != nullptr) safe->showAudioSettings(); });
        menu.addSeparator();
        menu.addItem (MainMenu::makeItem ("Exit", "Alt+F4", true, []
        {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                app->systemRequestedQuit();
        }));
        return menu;
    }

    juce::String MainComponent::describeBars (core::note_ops::BeatRange range, double beatsPerBar)
    {
        return arrangement::ArrangementModel::describeBars (range, beatsPerBar);
    }

    void MainComponent::chooseFilesToImport()
    {
        auto& formats = session.getEngine().getAudioFileFormatManager().readFormatManager;
        const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        importChooser = std::make_unique<juce::FileChooser> ("Import audio or MIDI", folder,
                                                             formats.getWildcardForAllFormats() + ";*.mid;*.midi");
        importChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::canSelectMultipleItems,
                                    [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                safe->addFilesAtPlayhead (fc.getResults());
        });
    }

    void MainComponent::addFilesAtPlayhead (const juce::Array<juce::File>& files)
    {
        if (files.isEmpty())
            return;

        // Onto the current track if it suits them (else new tracks), at the playhead.
        auto& model = arrangementModel;
        const auto beat = model.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getPositionSeconds())).inBeats();
        const auto error = model.importFiles (files, model.indexOf (model.getCurrentTrack()), beat);

        if (error.isNotEmpty())
            showWarning (error);
        else
            showMessage (files.size() == 1 ? "Added " + files.getFirst().getFileName()
                                           : "Added " + juce::String (files.size()) + " files");
    }

    void MainComponent::checkLoudness()
    {
        // Rendering takes over the engine: stop first, or playback would resume over it.
        transport.stop();

        juce::String error;
        const auto measured = audio::loudness::measureSong (project.getEdit(), error);

        if (! measured.has_value())
        {
            showWarning (error);
            return;
        }

        const auto advice = audio::loudness::advise (*measured);
        const auto canApply = std::abs (advice.gainDb) >= 0.1f;
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                                .withTitle ("Loudness for streaming")
                                                .withMessage (advice.text)
                                                .withAssociatedComponent (this);

        if (canApply)
            options = options.withButton ((advice.gainDb > 0.0f ? "Raise the master by " : "Lower the master by ")
                                              + juce::String (std::abs (advice.gainDb), 1) + " dB", 1);

        if (advice.peakLimited)
            options = options.withButton ("Use the limiter", 2);

        options = options.withButton ("Close", 0);

        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this), gain = advice.gainDb] (int result)
        {
            if (safe == nullptr)
                return;

            if (result == 1)
                safe->applyLoudnessGain (gain);
            else if (result == 2)
                safe->raiseWithLimiter();
        });
    }

    void MainComponent::applyLoudnessGain (float gainDb)
    {
        project.beginGesture ("Master volume for streaming");
        const auto added = audio::loudness::addToMasterVolume (project.getEdit(), gainDb);
        auto text = "Master volume " + juce::String (added >= 0.0f ? "+" : "") + juce::String (added, 1) + " dB";

        if (std::abs (added - gainDb) > 0.05f)
            text << " (as far as its fader goes)";

        showMessage (text);
    }

    void MainComponent::raiseWithLimiter()
    {
        transport.stop();

        // It renders the song a few times: say how far it got.
        juce::MouseCursor::showWaitCursor();
        project.beginGesture ("Limiter for streaming");
        juce::String error;
        const auto result = audio::loudness::raiseWithLimiter (project.getEdit(), error, audio::loudness::streamingLufs,
                                                               [this] (const juce::String& step)
        {
            showMessage (step);

            if (auto* peer = getPeer())
                peer->performAnyPendingRepaintsNow();
        });

        juce::MouseCursor::hideWaitCursor();

        if (! result.has_value())
        {
            showWarning (error);
            return;
        }

        showMessage ("Limiter on the master at +" + juce::String (result->gainDb, 1) + " dB: the song is now "
                     + juce::String (result->after.lufs, 1) + " LUFS (was " + juce::String (result->before.lufs, 1)
                     + "), peaks at " + juce::String (result->after.peakDb, 1) + " dB");
    }

    void MainComponent::checkMix()
    {
        // Rendering takes over the engine: stop first, or playback would resume over it.
        transport.stop();

        // It holds the window up for a while (the song, then each track): say how far it got.
        juce::MouseCursor::showWaitCursor();
        juce::String error;
        const auto report = audio::mix_check::check (project.getEdit(), error, [this] (const juce::String& step)
        {
            showMessage (step);

            if (auto* peer = getPeer())
                peer->performAnyPendingRepaintsNow();
        });

        juce::MouseCursor::hideWaitCursor();

        if (! report.has_value())
        {
            showWarning (error);
            return;
        }

        const auto problems = audio::mix_check::findProblems (*report).size();
        showMessage (problems == 0 ? juce::String ("Mix Doctor: nothing stands out")
                                   : "Mix Doctor: " + juce::String (problems) + (problems == 1 ? " thing" : " things") + " to look at");

        const auto fixes = audio::mix_check::findFixes (*report, project.getEdit());
        auto text = audio::mix_check::describe (*report);

        if (! fixes.isEmpty())
            text << "\n\n\"Fix the levels\" will " << fixes.descriptions.joinIntoString ("; ") << " (Ctrl+Z puts it back).";

        auto options = juce::MessageBoxOptions().withIconType (problems == 0 ? juce::MessageBoxIconType::InfoIcon
                                                                             : juce::MessageBoxIconType::WarningIcon)
                                                .withTitle ("Mix Doctor")
                                                .withMessage (text)
                                                .withAssociatedComponent (this);

        if (! fixes.isEmpty())
            options = options.withButton ("Fix the levels", 1);

        juce::AlertWindow::showAsync (options.withButton ("Close", 0),
                                      [safe = juce::Component::SafePointer<MainComponent> (this), fixes] (int result)
        {
            if (result == 1 && safe != nullptr)
                safe->fixMix (fixes);
        });
    }

    void MainComponent::fixMix (const audio::mix_check::Fixes& fixes)
    {
        transport.stop();
        juce::MouseCursor::showWaitCursor();
        project.beginGesture ("Mix Doctor fixes");

        juce::String error;
        const auto done = audio::mix_check::applyFixes (project.getEdit(), fixes, error, [this] (const juce::String& step)
        {
            showMessage (step);

            if (auto* peer = getPeer())
                peer->performAnyPendingRepaintsNow();
        });

        juce::MouseCursor::hideWaitCursor();

        if (error.isNotEmpty())
            showWarning (error);
        else
            showMessage (done.isEmpty() ? juce::String ("Mix Doctor: nothing needed changing")
                                        : "Mix Doctor: " + done.joinIntoString (", ") + " (Ctrl+Z puts it back)");
    }

    void MainComponent::chooseMidiExport()
    {
        const auto name = juce::File::createLegalFileName (fileCommands != nullptr ? fileCommands->getProjectName() : juce::String ("Song"));
        const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        importChooser = std::make_unique<juce::FileChooser> ("Export MIDI", folder.getChildFile (name + ".mid"), "*.mid");
        importChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (safe == nullptr || file == juce::File())
                return;

            if (! core::midi_files::isMidiFile (file))
                file = file.withFileExtension ("mid");

            const auto error = core::midi_files::writeSong (safe->project, file);

            if (error.isNotEmpty())
                safe->showWarning (error);
            else
                safe->showMessage ("Exported " + file.getFileName());
        });
    }

    void MainComponent::showAudioSettings()
    {
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Audio settings";
        options.dialogBackgroundColour = theme::colour::background;
        options.useNativeTitleBar = true;
        options.resizable = false;

        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (audio::getDeviceManager (session),
                                                                              0, 2, 1, 2, false, false, true, false);
        selector->setSize (500, 440);
        options.content.setOwned (selector.release());
        options.launchAsync();

        statusBar.setWarning ({});
    }

    void MainComponent::showAbout()
    {
        auto* app = juce::JUCEApplicationBase::getInstance();
        const auto version = app != nullptr ? app->getApplicationVersion() : juce::String();

        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                                               .withTitle ("About Harmonia")
                                                               .withMessage ("Harmonia " + version + "\n\n"
                                                                             "A free music studio (DAW) with open source code.\n"
                                                                             "Made with JUCE and Tracktion Engine.\n\n"
                                                                             "github.com/Mapelisin/Harmonia")
                                                               .withButton ("Close", 0)
                                                               .withAssociatedComponent (this),
                                      nullptr);
    }
}
