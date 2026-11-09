#include "ui/TransportBar.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    TransportBar::TransportBar (audio::Transport& t, te::Edit& e, NoteInput& n, arrangement::ArrangementModel& a,
                                audio::automation::Writer& w)
        : transport (t), edit (e), noteInput (n), arrangementModel (a), automationWriter (w)
    {
        toStartButton.setTooltip ("Back to the start (Home)");
        toStartButton.onClick = [this]
        {
            if (onGoToStart != nullptr)
                onGoToStart();

            refresh();
        };

        stopButton.setTooltip ("Stop (Space); while stopped: back to the start");
        stopButton.onClick = [this]
        {
            if (transport.isPlayRequested() || transport.isPlaying())
                transport.stop();
            else if (onGoToStart != nullptr)
                onGoToStart();

            refresh();
        };

        playButton.setTooltip ("Play / stop (Space)");
        playButton.onClick = [this]
        {
            transport.togglePlayStop();
            refresh();
        };

        recordButton.setTooltip ("Record (R): notes played while the song plays go into the clip open in the piano roll,\n"
                                 "and armed audio tracks record their input (straight on from where the song is).\n"
                                 "In Song mode with a loop region, audio is recorded round the loop: a take each time.");
        recordButton.onClick = [this] { noteInput.setRecordArmed (! noteInput.isRecordArmed()); };

        writeButton.setTooltip ("Write automation (Ctrl+Shift+A): while the song plays, the knobs and faders of tracks you move\n"
                                "(with the mouse or a MIDI control) are written into their automation lines.\n"
                                "Let go, and the line goes on as it was.");
        writeButton.onClick = [this]
        {
            automationWriter.setOn (! automationWriter.isOn());
            refresh();
        };

        clickButton.setTooltip ("Metronome (Ctrl+M)");
        clickButton.onClick = [this]
        {
            edit.clickTrackEnabled = ! edit.clickTrackEnabled.get();
            refresh();
        };

        countInButton.setTooltip ("Count-in: metronome clicks before a recording of audio starts, to get ready\n"
                                  "(off, 1 bar or 2 bars). The recording still starts where Play was pressed.");
        countInButton.onClick = [this] { showCountInMenu(); };

        modeButton.setTooltip ("What Play plays (L): the clip open in the piano roll, looped (Clip),\n"
                               "or the arrangement (Song: the loop region set with Ctrl+drag on its ruler, or the whole song).\n"
                               "Like PAT / SONG in FL Studio.");
        modeButton.onClick = [this]
        {
            if (onPlayModeToggled != nullptr)
                onPlayModeToggled();
        };

        keysButton.setTooltip ("Play notes on the computer keyboard (Ctrl+T).\n"
                               "Z S X D C V ... and Q 2 W 3 E R ... like piano keys; Up / Down: octave.\n"
                               "Works with any keyboard layout. One-letter shortcuts are off while it is on.");
        keysButton.onClick = [this]
        {
            auto& keyboard = noteInput.getComputerKeyboard();
            keyboard.setEnabled (! keyboard.isEnabled());
        };

        const auto& modes = core::getAllInputQuantizeModes();

        for (size_t i = 0; i < modes.size(); ++i)
            quantizeBox.addItem (core::getName (modes[i]), (int) i + 1);

        quantizeBox.setTooltip ("How recorded notes are put on the grid (the grid is the piano roll's Snap)");
        quantizeBox.onChange = [this, &modes]
        {
            if (const auto index = quantizeBox.getSelectedId() - 1; index >= 0)
                noteInput.setQuantize (modes[(size_t) index]);
        };

        // The tempo: drag it, type it (double-click) or tap it; each change is one undo step.
        auto& project = arrangementModel.getProject();
        tempoBox.setRange (core::tempo::minBpm, core::tempo::maxBpm);
        tempoBox.setSteps (1.0, 0.1);
        tempoBox.textFromValue = [] (double bpm) { return core::tempo::toText (bpm); };
        tempoBox.setTooltip ("Tempo: drag up / down (Shift: finer), mouse wheel, or double-click and type.\n"
                             "Clips, notes and automation stay on their beats; audio clips keep their speed.");
        tempoBox.onGestureStart = [&project] { project.beginGesture ("Change tempo"); };
        tempoBox.onValueChange = [&project] (double bpm) { project.setTempoBpm (bpm); };

        timeSigButton.setTooltip ("Time signature: beats per bar");
        timeSigButton.onClick = [this] { showTimeSigMenu(); };

        tapButton.setTooltip ("Tap tempo: click in time with the music (4 clicks or more); a pause starts over");
        tapButton.onClick = [this] { tap (juce::Time::getMillisecondCounterHiRes() / 1000.0); };

        recordButton.setIconColour (theme::colour::record);
        recordButton.setOnColour (theme::colour::record.withAlpha (0.45f));

        for (auto* c : std::initializer_list<juce::Component*> { &toStartButton, &stopButton, &playButton, &recordButton, &writeButton,
                                                                 &positionDisplay, &tempoBox, &timeSigButton, &tapButton,
                                                                 &clickButton, &countInButton, &modeButton,
                                                                 &keysButton, &quantizeBox })
        {
            c->setWantsKeyboardFocus (false);
            addAndMakeVisible (c);
        }

        refresh();
        startTimerHz (20);
    }

    TransportBar::~TransportBar()
    {
        stopTimer();
    }

    namespace
    {
        constexpr int iconButtonWidth = 32, iconButtonHeight = 28, gap = 4, groupGap = 16;
        constexpr int positionWidth = 196, positionHeight = 32, quantizeWidth = 150, writeWidth = 50;
    }

    int TransportBar::getPreferredWidth()
    {
        return 4 * (iconButtonWidth + gap) + writeWidth + groupGap   // to start, stop, play, record, write
             + positionWidth + groupGap
             + 78 + gap + 40 + gap + 40 + groupGap              // tempo, time signature, tap
             + iconButtonWidth + gap + 76 + gap + 54 + groupGap // metronome, count-in, clip / song
             + 74 + gap + quantizeWidth;                        // keys, quantize
    }

    void TransportBar::resized()
    {
        auto area = getLocalBounds();
        const auto place = [&area] (juce::Component& c, int width, int height = theme::size::buttonHeight, int after = gap)
        {
            c.setBounds (area.removeFromLeft (width).withSizeKeepingCentre (width, height));
            area.removeFromLeft (after);
        };

        place (toStartButton, iconButtonWidth, iconButtonHeight);
        place (stopButton, iconButtonWidth, iconButtonHeight);
        place (playButton, iconButtonWidth, iconButtonHeight);
        place (recordButton, iconButtonWidth, iconButtonHeight);
        place (writeButton, writeWidth, theme::size::buttonHeight, groupGap);
        place (positionDisplay, positionWidth, positionHeight, groupGap);
        place (tempoBox, 78);
        place (timeSigButton, 40);
        place (tapButton, 40, theme::size::buttonHeight, groupGap);
        place (clickButton, iconButtonWidth, iconButtonHeight);
        place (countInButton, 76);
        place (modeButton, 54, theme::size::buttonHeight, groupGap);
        place (keysButton, 74);
        place (quantizeBox, juce::jlimit (0, quantizeWidth, area.getWidth()));
    }

    void TransportBar::tap (double seconds)
    {
        if (const auto bpm = tapTempo.tap (seconds))
        {
            auto& project = arrangementModel.getProject();

            if (std::abs (*bpm - project.getTempoBpm()) >= 0.05)
            {
                project.beginGesture ("Tap tempo");
                project.setTempoBpm (*bpm);
            }
        }

        refresh();
    }

    void TransportBar::setBeatsPerBar (int beats)
    {
        auto& project = arrangementModel.getProject();

        if (beats != project.getBeatsPerBar())
        {
            project.beginGesture ("Time signature");
            project.setBeatsPerBar (beats);
        }

        refresh();
    }

    void TransportBar::showTimeSigMenu()
    {
        const auto current = arrangementModel.getProject().getBeatsPerBar();
        juce::Component::SafePointer<TransportBar> safe (this);
        juce::PopupMenu menu;
        menu.addSectionHeader ("Time signature");

        for (const auto beats : core::tempo::getBeatsPerBarChoices())
            menu.addItem (juce::String (beats) + "/4", true, beats == current, [safe, beats]
            {
                if (safe != nullptr)
                    safe->setBeatsPerBar (beats);
            });

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&timeSigButton));
    }

    void TransportBar::timerCallback()
    {
        refresh();
    }

    void TransportBar::showCountInMenu()
    {
        using CountIn = audio::Transport::CountIn;
        const auto current = transport.getCountIn();
        juce::Component::SafePointer<TransportBar> safe (this);
        juce::PopupMenu menu;
        menu.addSectionHeader ("Count-in before recording audio");

        for (const auto& [countIn, name] : { std::pair<CountIn, const char*> { CountIn::off, "Off" },
                                             { CountIn::oneBar, "1 bar" },
                                             { CountIn::twoBars, "2 bars" } })
        {
            menu.addItem (name, true, countIn == current, [safe, countIn]
            {
                if (safe != nullptr)
                {
                    safe->transport.setCountIn (countIn);
                    safe->refresh();
                }
            });
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&countInButton));
    }

    void TransportBar::refresh()
    {
        const auto active = transport.isPlayRequested() || transport.isPlaying();
        playButton.setToggleState (active, juce::dontSendNotification);
        playButton.setIconColour (active ? theme::mixer::meterLow : theme::colour::textPrimary);

        // Armed: a dim red; recording: full red with a white dot.
        const auto recording = noteInput.isRecording() || transport.isRecording();
        recordButton.setToggleState (recording || noteInput.isRecordArmed(), juce::dontSendNotification);
        recordButton.setOnColour (recording ? theme::colour::record : theme::colour::record.withAlpha (0.4f));
        recordButton.setIconColour (recording ? juce::Colours::white : theme::colour::record);

        clickButton.setToggleState (edit.clickTrackEnabled.get(), juce::dontSendNotification);

        const auto writing = automationWriter.isOn();
        writeButton.setColour (juce::TextButton::buttonColourId, writing ? theme::colour::record.withAlpha (0.55f) : theme::colour::surfaceRaised);
        writeButton.setColour (juce::TextButton::textColourOffId, writing ? juce::Colours::white : theme::colour::textSecondary);

        const auto countIn = transport.getCountIn();
        countInButton.setButtonText (countIn == audio::Transport::CountIn::twoBars ? "Count 2 bars"
                                   : countIn == audio::Transport::CountIn::oneBar  ? "Count 1 bar"
                                                                                   : "Count-in");
        countInButton.setColour (juce::TextButton::buttonColourId,
                                 countIn != audio::Transport::CountIn::off ? theme::colour::accent.withAlpha (0.35f)
                                                                           : theme::colour::surfaceRaised);

        const auto song = arrangementModel.getPlayMode() == arrangement::PlayMode::song;
        modeButton.setButtonText (song ? "Song" : "Clip");
        modeButton.setColour (juce::TextButton::buttonColourId,
                              song ? theme::colour::warning.withAlpha (0.35f) : theme::colour::surfaceRaised);

        const auto& keyboard = noteInput.getComputerKeyboard();
        keysButton.setButtonText ("Keys  C" + juce::String (keyboard.getOctave()));
        keysButton.setColour (juce::TextButton::buttonColourId,
                              keyboard.isEnabled() ? theme::colour::accent.withAlpha (0.35f) : theme::colour::surfaceRaised);

        const auto& modes = core::getAllInputQuantizeModes();
        const auto modeIndex = (int) (std::find (modes.begin(), modes.end(), noteInput.getQuantize()) - modes.begin());
        quantizeBox.setSelectedId (modeIndex + 1, juce::dontSendNotification);

        const auto seconds = transport.getPositionSeconds();

        // Counting in, the song is before where the recording starts (maybe before its start).
        if (transport.isCountingIn())
        {
            const auto& tempo = edit.tempoSequence;
            const auto beatsLeft = tempo.toBeats (te::TimePosition::fromSeconds (transport.getRecordStartSeconds())).inBeats()
                                 - tempo.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats();
            positionDisplay.setText ("Count " + juce::String ((int) std::ceil (beatsLeft - 1.0e-6)), "get ready");
        }
        else
        {
            // Recording round a loop: which take this is, in place of the clock.
            const auto take = transport.getTakeNumber();
            positionDisplay.setText (PositionDisplay::barsBeatsText (edit.tempoSequence, seconds),
                                     take > 0 ? "take " + juce::String (take) : PositionDisplay::clockText (seconds));
        }

        auto& project = arrangementModel.getProject();
        tempoBox.setValue (project.getTempoBpm());
        const auto timeSig = juce::String (project.getBeatsPerBar()) + "/4";

        if (timeSigButton.getButtonText() != timeSig)
            timeSigButton.setButtonText (timeSig);
    }
}
