#include "ui/MainComponent.h"
#include "core/MidiFiles.h"
#include "core/Tracks.h"
#include "audio/DeviceStatus.h"
#include "ui/ComputerKeyboard.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int trackRowHeight = 40;
        constexpr int appNameWidth = 90, fileButtonWidth = 50, summaryWidth = 150, tabWidth = 96;
        constexpr int lowerPanelTabs = 1001;
    }

    MainComponent::MainComponent (core::Session& s, core::Project& p)
        : session (s), project (p)
    {
        // Clicks on widgets that don't take focus land here, so Space and the editing
        // shortcuts keep working wherever the user clicked.
        setWantsKeyboardFocus (true);

        pianoRollModel.setAuditioner (&auditioner);
        pianoRollModel.addListener (this);
        arrangementModel.addListener (this);
        arrangementModel.onCurrentChanged = [this] { currentChanged(); };

        midiInputs.onMessage = [this] (const juce::MidiMessage& m, double wallMs) { noteInput.handleMidiMessage (m, wallMs); };
        midiInputs.onDevicesChanged = [this] { updateMidiStatus(); };
        noteInput.onStateChanged = [this]
        {
            pianoRoll.setLetterShortcutsEnabled (! noteInput.getComputerKeyboard().isEnabled());
            transportBar.refresh();
        };
        updateMidiStatus();

        transportBar.onPlayModeToggled = [this] { togglePlayMode(); };
        arrangement.onPositionChosen = [this] (double beat) { playFrom (beat); };

        trackNameLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        trackNameLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);

        fileButton.setTooltip ("New, open, save, export WAV");
        fileButton.onClick = [this] { showFileMenu(); };

        audioSettingsButton.setEnabled (session.devicesEnabled());
        audioSettingsButton.onClick = [this] { showAudioSettings(); };

        // The lower part shows the piano roll, the automation or the mixer (F7 / F8 / F9).
        pianoRollTab.setTooltip ("Piano roll of the open clip (F7)");
        automationTab.setTooltip ("Automation: lines that change volume, pan, sends or effects over the song (F8)");
        mixerTab.setTooltip ("Mixer: effects, volume, pan (F9)");

        for (auto* tab : { &pianoRollTab, &automationTab, &mixerTab })
        {
            tab->setClickingTogglesState (true);
            tab->setRadioGroupId (lowerPanelTabs);
            tab->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.35f));
        }

        pianoRollTab.setToggleState (true, juce::dontSendNotification);
        pianoRollTab.onClick = [this] { showLowerPanel (LowerPanel::pianoRoll); };
        automationTab.onClick = [this] { showLowerPanel (LowerPanel::automation); };
        mixerTab.onClick = [this] { showLowerPanel (LowerPanel::mixer); };

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

        for (auto* c : std::initializer_list<juce::Component*> { &fileButton, &audioSettingsButton, &pianoRollTab, &automationTab, &mixerTab })
            c->setWantsKeyboardFocus (false);

        // Arrangement above, piano roll below; the bar between them can be dragged.
        splitLayout.setItemLayout (0, 110.0, -1.0, 250.0);
        splitLayout.setItemLayout (1, 6.0, 6.0, 6.0);
        splitLayout.setItemLayout (2, 200.0, -1.0, -0.62);

        for (auto* c : std::initializer_list<juce::Component*> { &transportBar, &fileButton, &audioSettingsButton, &trackNameLabel,
                                                                 &pianoRollTab, &automationTab, &mixerTab, &arrangement, &splitter, &pianoRoll,
                                                                 &statusBar })
            addAndMakeVisible (c);

        addChildComponent (mixer);
        addChildComponent (automationPanel);

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

    void MainComponent::togglePlayMode()
    {
        using arrangement::PlayMode;
        arrangementModel.setPlayMode (arrangementModel.getPlayMode() == PlayMode::clip ? PlayMode::song : PlayMode::clip);
        updateLoop();
        transportBar.refresh();
        arrangement.repaint();
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
        repaint (getLocalBounds().removeFromTop (theme::size::headerHeight));
    }

    //==============================================================================
    juce::String MainComponent::getProjectSummary() const
    {
        const auto tracks = project.getNumTracks();
        const auto name = fileCommands != nullptr ? fileCommands->getProjectName() : juce::String ("Untitled");
        return name + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (tracks) + (tracks == 1 ? " track" : " tracks");
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        auto header = getLocalBounds().removeFromTop (theme::size::headerHeight);
        g.setColour (theme::colour::surface);
        g.fillRect (header);
        g.setColour (theme::colour::border);
        g.fillRect (header.removeFromBottom (1));

        auto text = header.reduced (theme::size::padding, 0);
        g.setColour (theme::colour::textPrimary);
        g.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        g.drawText ("Harmonia", text.removeFromLeft (appNameWidth), juce::Justification::centredLeft);

        text.removeFromLeft (fileButtonWidth + theme::size::padding);
        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (theme::size::fontSize));
        g.drawText (getProjectSummary(), text.removeFromLeft (summaryWidth), juce::Justification::centredLeft, true);

        // The track row: the current track's name over the headers, its instrument to the right.
        auto trackRow = getLocalBounds().withTrimmedTop (theme::size::headerHeight).removeFromTop (trackRowHeight);
        g.setColour (theme::colour::border);
        g.fillRect (trackRow.removeFromBottom (1));
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (theme::size::headerHeight).reduced (theme::size::padding, 0);
        statusBar.setBounds (area.removeFromBottom (theme::size::statusBarHeight));

        audioSettingsButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, theme::size::buttonHeight));
        header.removeFromLeft (appNameWidth);
        fileButton.setBounds (header.removeFromLeft (fileButtonWidth).withSizeKeepingCentre (fileButtonWidth, theme::size::buttonHeight));
        header.removeFromLeft (theme::size::padding + summaryWidth);
        transportBar.setBounds (header.removeFromLeft (std::max (0, std::min (840, header.getWidth() - theme::size::padding))));

        auto trackRow = area.removeFromTop (trackRowHeight);
        trackNameLabel.setBounds (trackRow.removeFromLeft (arrangement::Arrangement::headerWidth).reduced (theme::size::padding / 2, 0));

        auto tabs = trackRow.removeFromRight (3 * tabWidth + theme::size::padding).withTrimmedRight (theme::size::padding);
        mixerTab.setBounds (tabs.removeFromRight (tabWidth).withSizeKeepingCentre (tabWidth, theme::size::buttonHeight));
        automationTab.setBounds (tabs.removeFromRight (tabWidth).withSizeKeepingCentre (tabWidth, theme::size::buttonHeight));
        pianoRollTab.setBounds (tabs.removeFromRight (tabWidth).withSizeKeepingCentre (tabWidth, theme::size::buttonHeight));

        if (instrumentPanel != nullptr)
            instrumentPanel->setBounds (trackRow.reduced (theme::size::padding / 2, 4));

        juce::Component* parts[] = { &arrangement, &splitter, &pianoRoll };
        splitLayout.layOutComponents (parts, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
        mixer.setBounds (pianoRoll.getBounds());
        automationPanel.setBounds (pianoRoll.getBounds());
    }

    void MainComponent::showLowerPanel (LowerPanel panel)
    {
        const auto hadFocus = pianoRoll.hasKeyboardFocus (true) || mixer.hasKeyboardFocus (true)
                           || automationPanel.hasKeyboardFocus (true);

        lowerPanel = panel;
        pianoRollTab.setToggleState (panel == LowerPanel::pianoRoll, juce::dontSendNotification);
        automationTab.setToggleState (panel == LowerPanel::automation, juce::dontSendNotification);
        mixerTab.setToggleState (panel == LowerPanel::mixer, juce::dontSendNotification);
        pianoRoll.setVisible (panel == LowerPanel::pianoRoll);
        automationPanel.setVisible (panel == LowerPanel::automation);
        mixer.setVisible (panel == LowerPanel::mixer);

        if (panel == LowerPanel::automation)
            automationPanel.refresh();

        // Keys keep working: the piano roll takes them back, or this component does.
        if (panel == LowerPanel::pianoRoll && (hadFocus || ! hasKeyboardFocus (true)) && pianoRoll.isShowing())
            pianoRoll.grabKeyboardFocus();
        else if (panel != LowerPanel::pianoRoll && hadFocus && isShowing())
            grabKeyboardFocus();
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            transport.togglePlayStop();
            return true;
        }

        auto& keyboard = noteInput.getComputerKeyboard();
        const auto mods = key.getModifiers();
        const auto letter = keys::getLetter (key);
        const auto code = key.getKeyCode();

        if (! mods.isAnyModifierKeyDown()
            && (code == juce::KeyPress::F7Key || code == juce::KeyPress::F8Key || code == juce::KeyPress::F9Key))
        {
            showLowerPanel (code == juce::KeyPress::F7Key ? LowerPanel::pianoRoll
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

    //==============================================================================
    void MainComponent::showFileMenu()
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
        menu.addSeparator();
        menu.addItem ("Save  (Ctrl+S)", hasCommands, false, run (&FileCommands::saveProject));
        menu.addItem ("Save as...  (Ctrl+Shift+S)", hasCommands, false, run (&FileCommands::saveProjectAs));
        menu.addSeparator();
        menu.addItem ("Import audio or MIDI...", [safe] { if (safe != nullptr) safe->chooseFilesToImport(); });
        menu.addItem ("Export WAV...  (Ctrl+R)", hasCommands, false, run (&FileCommands::exportWav));
        menu.addItem ("Export stems (WAV per track)...", hasCommands, false, run (&FileCommands::exportStems));
        menu.addItem ("Export MIDI...", [safe] { if (safe != nullptr) safe->chooseMidiExport(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton));
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
            const auto files = fc.getResults();

            if (safe == nullptr || files.isEmpty())
                return;

            // Onto the current track if it is an audio track (else new tracks), at the playhead.
            auto& model = safe->arrangementModel;
            const auto beat = model.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (safe->transport.getPositionSeconds())).inBeats();
            const auto error = model.importFiles (files, model.indexOf (model.getCurrentTrack()), beat);

            if (error.isNotEmpty())
                safe->showWarning (error);
            else
                safe->showMessage (files.size() == 1 ? "Added " + files.getFirst().getFileName()
                                                     : "Added " + juce::String (files.size()) + " files");
        });
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
}
