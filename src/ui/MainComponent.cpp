#include "ui/MainComponent.h"
#include "audio/DrumKit.h"
#include "audio/Instruments.h"
#include "audio/Sampler.h"
#include "core/Backups.h"
#include "core/SongFiles.h"
#include "core/Drums.h"
#include "core/Markers.h"
#include "core/MidiFiles.h"
#include "core/Tracks.h"
#include "audio/DeviceStatus.h"
#include "audio/Loudness.h"
#include "audio/MixCheck.h"
#include "ui/ComputerKeyboard.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/settings/SettingsPanel.h"

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

        // A MIDI control assigned to a setting (or being assigned) moves that, not the notes.
        midiInputs.onMessage = [this] (const juce::MidiMessage& m, double wallMs)
        {
            if (! midiControls.handle (m))
                noteInput.handleMidiMessage (m, wallMs);
        };
        midiControls.onMessage = [this] (const juce::String& text) { showMessage (text); };
        automationWriter.onChanged = [this] { transportBar.refresh(); };
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
        pianoRollTab.setTooltip ("Piano roll of the open clip; on an audio track, the audio clip editor (F7)");
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
        soundBrowser.onSoundMenu = [this] (juce::PopupMenu& menu, const juce::File& file)
        {
            addSamplerItems (menu, file);
            addDrumSoundItems (menu, file);
        };

        // A click on the audio clip editor's waveform moves the playhead (the play mode stays).
        audioEditor.onPositionChosen = [this] (double beat)
        {
            transport.setPositionSeconds (project.getEdit().tempoSequence.toTime (te::BeatPosition::fromBeats (beat)).inSeconds());
            transportBar.refresh();
        };
        audioEditor.onMessage = [this] (const juce::String& text, bool isProblem)
        {
            if (isProblem)
                statusBar.setWarning (text);
            else
                statusBar.setMessage (text);
        };

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

        addChildComponent (audioEditor);
        addChildComponent (stepSequencer);
        pianoRoll.onShowSteps = [this] { setStepsShown (true); };
        stepSequencer.onShowNotes = [this] { setStepsShown (false); };
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
        midiControls.onMessage = nullptr;
        automationWriter.onChanged = nullptr;
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

        updateEditor();
        audioEditor.refresh();

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

        // Recording with a loop region set in song mode goes round it: a take each time.
        transport.setRecordsTakes (arrangementModel.getPlayMode() == arrangement::PlayMode::song
                                   && arrangementModel.getLoopRegion().has_value());
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
        return arrangement.hasKeyboardFocus (true) || lowerPanel != LowerPanel::pianoRoll || editsAudio();
    }

    void MainComponent::updateEditor()
    {
        // An audio track's clips are sound files: the audio clip editor takes the piano roll's place.
        const auto audio = editsAudio();
        const auto pianoRollHadFocus = pianoRoll.hasKeyboardFocus (true);

        pianoRollTab.setButtonText (getEditorName());
        pianoRoll.setVisible (lowerPanel == LowerPanel::pianoRoll && ! audio && ! stepsShown);
        stepSequencer.setVisible (lowerPanel == LowerPanel::pianoRoll && ! audio && stepsShown);
        audioEditor.setVisible (lowerPanel == LowerPanel::pianoRoll && audio);

        if (stepSequencer.isVisible())
            stepSequencer.refresh();

        // Keys keep working when the piano roll goes.
        if (pianoRollHadFocus && ! pianoRoll.isVisible() && isShowing())
            grabKeyboardFocus();
    }

    void MainComponent::setStepsShown (bool shouldShow)
    {
        if (stepsShown == shouldShow && lowerPanel == LowerPanel::pianoRoll)
            return;

        stepsShown = shouldShow;
        showLowerPanel (LowerPanel::pianoRoll);
    }

    bool MainComponent::editsAudio() const
    {
        auto* track = arrangementModel.getCurrentTrack();
        return track != nullptr && core::tracks::isAudio (*track);
    }

    juce::String MainComponent::getEditorName() const
    {
        return editsAudio() ? "Audio clip" : "Piano roll";
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
        audioEditor.setBounds (pianoRoll.getBounds());
        stepSequencer.setBounds (pianoRoll.getBounds());
        mixer.setBounds (pianoRoll.getBounds());
        automationPanel.setBounds (pianoRoll.getBounds());
        soundBrowser.setBounds (pianoRoll.getBounds());
    }

    void MainComponent::showLowerPanel (LowerPanel panel)
    {
        const auto hadFocus = pianoRoll.hasKeyboardFocus (true) || mixer.hasKeyboardFocus (true) || audioEditor.hasKeyboardFocus (true)
                           || automationPanel.hasKeyboardFocus (true) || soundBrowser.hasKeyboardFocus (true);
        const auto audio = editsAudio();

        lowerPanel = panel;
        pianoRollTab.setToggleState (panel == LowerPanel::pianoRoll, juce::dontSendNotification);
        automationTab.setToggleState (panel == LowerPanel::automation, juce::dontSendNotification);
        mixerTab.setToggleState (panel == LowerPanel::mixer, juce::dontSendNotification);
        soundsTab.setToggleState (panel == LowerPanel::sounds, juce::dontSendNotification);
        updateEditor();
        automationPanel.setVisible (panel == LowerPanel::automation);
        mixer.setVisible (panel == LowerPanel::mixer);
        soundBrowser.setVisible (panel == LowerPanel::sounds);

        if (panel != LowerPanel::sounds)
            filePreview.stop();

        if (panel == LowerPanel::automation)
            automationPanel.refresh();

        // Keys keep working: the piano roll takes them back, or this component does.
        if (panel == LowerPanel::pianoRoll && ! audio && (hadFocus || ! hasKeyboardFocus (true)) && pianoRoll.isShowing())
            pianoRoll.grabKeyboardFocus();
        else if (panel == LowerPanel::sounds && soundBrowser.isShowing())
            soundBrowser.getList().grabKeyboardFocus();     // Up / Down go through the sounds
        else if ((panel != LowerPanel::pianoRoll || audio) && hadFocus && isShowing())
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

        // Alt+T: the step sequencer and the piano roll, in turn.
        if (mods.isAltDown() && ! mods.isCommandDown() && ! mods.isShiftDown() && letter == 'T' && ! editsAudio())
        {
            setStepsShown (! (stepsShown && lowerPanel == LowerPanel::pianoRoll));
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

            if (shift && (letter == 'A' || letter == 'L'))
            {
                if (letter == 'A') toggleAutomationWriting();
                else               toggleMidiLearn();

                return true;
            }

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
                case 'R': if (fileCommands != nullptr && ! shift) fileCommands->exportSong(); return ! shift;
                case 'T': if (! shift) keyboard.setEnabled (! keyboard.isEnabled()); return ! shift;
                case 'M': if (! shift) toggleMetronome(); return ! shift;
                default:  break;
            }

            // Ctrl+, as in most programs; the same key types "б" in the Russian layout.
            if (! shift && (code == ',' || code == 0x0431 || code == 0x0411))
            {
                showSettings();
                return true;
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
        // Nor do they while the piano roll is hidden behind the mixer, the automation or the
        // audio clip editor.
        if (arrangement.hasKeyboardFocus (true) || lowerPanel != LowerPanel::pianoRoll || editsAudio())
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

    void MainComponent::addDrumSoundItems (juce::PopupMenu& menu, const juce::File& file)
    {
        auto* track = arrangementModel.getCurrentTrack();

        if (track == nullptr || ! audio::instruments::playsDrumKit (*track))
            return;

        auto* drums = audio::instruments::get (*track);
        juce::PopupMenu keys;
        juce::Component::SafePointer<MainComponent> safe (this);

        // The drums of the kit, and whatever other keys have samples.
        for (int note = 0; note < 128; ++note)
        {
            const auto name = core::drums::getName (note);
            const auto sampled = audio::drum_kit::getSample (*drums, note).has_value();

            if (name.isEmpty() && ! sampled)
                continue;

            const auto key = juce::MidiMessage::getMidiNoteName (note, true, true, 4);
            keys.addItem ((name.isNotEmpty() ? name : "Key") + " (" + key + ")", true, sampled, [safe, file, note]
            {
                if (safe != nullptr)
                    safe->useAsDrumSound (file, note);
            });
        }

        menu.addSubMenu ("Use as a drum sound of " + track->getName(), keys);
    }

    void MainComponent::useAsDrumSound (const juce::File& file, int note)
    {
        auto* track = arrangementModel.getCurrentTrack();
        auto* drums = track != nullptr && audio::instruments::playsDrumKit (*track) ? audio::instruments::get (*track) : nullptr;

        if (drums == nullptr)
        {
            showWarning ("The current track doesn't play the drum machine");
            return;
        }

        if (const auto error = audio::drum_kit::setSample (*drums, note, file); error.isNotEmpty())
        {
            showWarning (error);
            return;
        }

        if (auto* machine = dynamic_cast<audio::drum_kit::Plugin*> (drums))
            machine->updateSamplesNow();

        // Heard at once.
        notePreview.noteOn (note, 100);
        notePreview.noteOff (note);
        pianoRoll.repaint();

        const auto drum = core::drums::getName (note);
        showMessage ((drum.isNotEmpty() ? drum : juce::String ("The key")) + " of " + track->getName() + " plays " + file.getFileNameWithoutExtension()
                     + " (Ctrl+Z: back)");
    }

    void MainComponent::collectFiles()
    {
        const auto projectFile = fileCommands != nullptr ? fileCommands->getProjectFile() : juce::File();

        if (projectFile == juce::File())
        {
            showWarning ("Save the song first (Ctrl+S): its sound files are collected next to it");
            return;
        }

        noteInput.finishTake();
        const auto result = core::song_files::collect (project, projectFile);
        repaint();

        auto text = result.describe();

        if (result.changed > 0)
            text << ". Save the song (Ctrl+S) to keep it so; Ctrl+Z takes it back";

        if (result.problems.isEmpty() && result.missing == 0)
            showMessage (text);
        else
            showWarning (text);
    }

    juce::String MainComponent::describeMissingFiles() const
    {
        constexpr int maxListed = 12;
        const auto missing = core::song_files::getMissing (project.getEdit());
        juce::StringArray lines;

        for (const auto& sound : missing)
        {
            if (lines.size() == maxListed)
            {
                lines.add ("...and " + juce::String ((int) missing.size() - maxListed) + " more");
                break;
            }

            lines.add (sound.file.getFileName() + "  (was in " + sound.file.getParentDirectory().getFullPathName()
                         + "; used by " + sound.usedBy.joinIntoString (", ") + ")");
        }

        return lines.joinIntoString ("\n");
    }

    void MainComponent::findMissingFiles()
    {
        const auto count = (int) core::song_files::getMissing (project.getEdit()).size();

        if (count == 0)
        {
            showMessage ("No sound file of the song is missing");
            return;
        }

        juce::Component::SafePointer<MainComponent> safe (this);

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (count == 1 ? juce::String ("A sound file is missing")
                                                                 : juce::String (count) + " sound files are missing")
                                          .withMessage (describeMissingFiles()
                                                          + "\n\nIf they were moved, choose the folder they are in now: Harmonia looks "
                                                            "for them by name there and in the folders inside it.")
                                          .withButton ("Look in a folder...", 1)
                                          .withButton ("Cancel", 0)
                                          .withAssociatedComponent (this),
                                      [safe] (int result)
        {
            if (safe == nullptr || result != 1)
                return;

            safe->importChooser = std::make_unique<juce::FileChooser> ("The folder the missing sounds are in",
                                                                     juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            safe->importChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                            [safe] (const juce::FileChooser& chooser)
            {
                if (safe != nullptr && chooser.getResult() != juce::File())
                    safe->lookForMissingFilesIn (chooser.getResult());
            });
        });
    }

    void MainComponent::lookForMissingFilesIn (const juce::File& folder)
    {
        noteInput.finishTake();
        const auto result = core::song_files::findIn (project, folder);
        repaint();

        if (result.found == 0)
            showWarning (result.stillMissing == 0 ? juce::String ("No sound file of the song is missing")
                                                  : "None of the missing files is in " + folder.getFullPathName());
        else if (result.stillMissing > 0)
            showWarning ("Found " + juce::String (result.found) + " of " + juce::String (result.found + result.stillMissing)
                           + " missing files; " + juce::String (result.stillMissing) + " still missing (File > Find missing files). Ctrl+Z: back");
        else
        {
            showWarning ({});       // the one about missing files is dealt with
            showMessage ("Found " + juce::String (result.found == 1 ? "the missing file" : "all " + juce::String (result.found) + " missing files")
                           + ". Save the song (Ctrl+S) to keep it so; Ctrl+Z takes it back");
        }
    }

    void MainComponent::addSamplerItems (juce::PopupMenu& menu, const juce::File& file)
    {
        juce::Component::SafePointer<MainComponent> safe (this);

        menu.addItem ("New sampler track playing this sound (play it with notes)", [safe, file]
        {
            if (safe != nullptr)
                safe->addSamplerTrack (file);
        });

        if (auto* track = arrangementModel.getCurrentTrack(); track != nullptr && audio::instruments::playsSampler (*track))
        {
            menu.addItem ("Play this sound in the sampler of " + track->getName(), [safe, file]
            {
                if (safe != nullptr)
                    safe->useInSampler (file);
            });
        }
    }

    void MainComponent::addSamplerTrack (const juce::File& file)
    {
        noteInput.finishTake();
        juce::String error;
        auto* track = arrangementModel.addSamplerTrack (file, error);

        if (track == nullptr)
        {
            showWarning (error.isNotEmpty() ? error : juce::String ("No more tracks fit into the song"));
            return;
        }

        showMessage (track->getName() + ": a new track that plays this sound with notes (the piano roll, the keyboard). Ctrl+Z: back");
    }

    void MainComponent::useInSampler (const juce::File& file)
    {
        auto* track = arrangementModel.getCurrentTrack();
        auto* instrument = track != nullptr && audio::instruments::playsSampler (*track) ? audio::instruments::get (*track) : nullptr;

        if (instrument == nullptr)
        {
            showWarning ("The current track doesn't play the sampler");
            return;
        }

        if (const auto error = audio::sampler::setFile (*instrument, file); error.isNotEmpty())
        {
            showWarning (error);
            return;
        }

        if (auto* plugin = dynamic_cast<audio::sampler::Plugin*> (instrument))
            plugin->updateSampleNow();

        pianoRoll.repaint();
        showMessage ("The sampler of " + track->getName() + " plays " + file.getFileNameWithoutExtension() + " (Ctrl+Z: back)");
    }

    void MainComponent::toggleAutomationWriting()
    {
        automationWriter.setOn (! automationWriter.isOn());
        showMessage (automationWriter.isOn() ? "Writing automation: knobs and faders moved while the song plays go into their lines (Ctrl+Shift+A: off)"
                                             : "Writing automation off");
    }

    void MainComponent::toggleMidiLearn()
    {
        if (midiControls.isLearning())
            midiControls.stopLearning();
        else
            midiControls.startLearning();
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

        // What the song was before each of the last saves.
        juce::PopupMenu versionsMenu;
        const auto projectFile = hasCommands ? fileCommands->getProjectFile() : juce::File();
        const auto versions = core::backups::list (projectFile);

        for (const auto& version : versions)
        {
            versionsMenu.addItem (version.getLabel(), [safe, versionFile = version.file]
            {
                if (safe != nullptr && safe->fileCommands != nullptr)
                    safe->fileCommands->openEarlierVersion (versionFile);
            });
        }

        if (! versions.empty())
        {
            versionsMenu.addSeparator();
            versionsMenu.addItem ("Show their folder", [projectFile] { core::backups::getFolder (projectFile).revealToUser(); });
        }

        menu.addSubMenu ("Earlier versions (kept at each save)", versionsMenu, ! versions.empty());
        menu.addSeparator();
        menu.addItem ("Collect all files into the song's folder", [safe] { if (safe != nullptr) safe->collectFiles(); });

        const auto numMissing = (int) core::song_files::getMissing (project.getEdit()).size();
        menu.addItem (numMissing > 0 ? "Find missing files (" + juce::String (numMissing) + ")..."
                                     : juce::String ("Find missing files (none are missing)"),
                      numMissing > 0, false, [safe] { if (safe != nullptr) safe->findMissingFiles(); });
        menu.addSeparator();
        menu.addItem ("Import audio or MIDI...", [safe] { if (safe != nullptr) safe->chooseFilesToImport(); });
        menu.addItem ("Export song (WAV, FLAC, MP3)...  (Ctrl+R)", hasCommands, false, run (&FileCommands::exportSong));
        menu.addItem ("Export stems (a file per track or group)...", hasCommands, false, run (&FileCommands::exportStems));

        const auto loop = arrangementModel.getLoopRegion();
        menu.addItem (loop.has_value() ? "Export the loop region as WAV (" + describeBars (*loop, arrangementModel.getBeatsPerBar()) + ")..."
                                       : juce::String ("Export the loop region as WAV (Ctrl+drag on the ruler first)"),
                      hasCommands && loop.has_value(), false, run (&FileCommands::exportLoopWav));
        menu.addItem ("Export MIDI...", [safe] { if (safe != nullptr) safe->chooseMidiExport(); });
        menu.addSeparator();
        menu.addItem ("Check loudness for streaming...", [safe] { if (safe != nullptr) safe->checkLoudness(); });
        menu.addItem ("Check the mix (Mix Doctor)...", [safe] { if (safe != nullptr) safe->checkMix(); });
        menu.addSeparator();
        menu.addItem (MainMenu::makeItem ("Settings (audio, MIDI, plugins)...", "Ctrl+,", true,
                                          [safe] { if (safe != nullptr) safe->showSettings(); }));
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

    void MainComponent::showSettings()
    {
        SettingsPanel::show (session);

        // A warning about the sound card is being dealt with.
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
