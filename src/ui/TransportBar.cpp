#include "ui/TransportBar.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    TransportBar::TransportBar (audio::Transport& t, te::Edit& e, NoteInput& n, arrangement::ArrangementModel& a)
        : transport (t), edit (e), noteInput (n), arrangementModel (a)
    {
        playButton.setTooltip ("Play / stop (Space)");
        playButton.onClick = [this]
        {
            transport.togglePlayStop();
            refresh();
        };

        recordButton.setTooltip ("Record (R): notes played while the song plays go into the clip open in the piano roll,\n"
                                 "and armed audio tracks record their input (straight on from where the song is)");
        recordButton.onClick = [this] { noteInput.setRecordArmed (! noteInput.isRecordArmed()); };

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

        positionLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        positionLabel.setFont (juce::FontOptions (theme::size::fontSize));
        positionLabel.setJustificationType (juce::Justification::centredLeft);

        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &recordButton, &clickButton, &countInButton, &modeButton,
                                                                 &positionLabel, &keysButton, &quantizeBox })
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

    void TransportBar::resized()
    {
        auto area = getLocalBounds();
        const auto place = [&area] (juce::Component& c, int width)
        {
            c.setBounds (area.removeFromLeft (width).withSizeKeepingCentre (width, theme::size::buttonHeight));
            area.removeFromLeft (4);
        };

        place (playButton, 60);
        place (recordButton, 50);
        place (clickButton, 54);
        place (countInButton, 82);
        place (modeButton, 54);
        area.removeFromLeft (theme::size::padding);
        place (positionLabel, 110);
        place (keysButton, 78);
        place (quantizeBox, 190);
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
        playButton.setButtonText (active ? "Stop" : "Play");
        playButton.setColour (juce::TextButton::buttonColourId,
                              active ? theme::colour::accent.withAlpha (0.35f) : theme::colour::surfaceRaised);

        const auto recordColour = noteInput.isRecording() || transport.isRecording() ? theme::colour::record
                                : noteInput.isRecordArmed() ? theme::colour::record.withAlpha (0.45f)
                                                            : theme::colour::surfaceRaised;
        recordButton.setColour (juce::TextButton::buttonColourId, recordColour);

        clickButton.setColour (juce::TextButton::buttonColourId,
                               edit.clickTrackEnabled.get() ? theme::colour::accent.withAlpha (0.35f) : theme::colour::surfaceRaised);

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
        const auto barsAndBeats = edit.tempoSequence.toBarsAndBeats (te::TimePosition::fromSeconds (seconds));

        auto text = juce::String (barsAndBeats.bars + 1) + "." + juce::String (barsAndBeats.getWholeBeats() + 1)
                  + "   " + juce::String (seconds, 2) + " s";

        // Counting in, the song is before where the recording starts (maybe before its start).
        if (transport.isCountingIn())
        {
            const auto& tempo = edit.tempoSequence;
            const auto beatsLeft = tempo.toBeats (te::TimePosition::fromSeconds (transport.getRecordStartSeconds())).inBeats()
                                 - tempo.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats();
            text = "Count-in " + juce::String ((int) std::ceil (beatsLeft - 1.0e-6));
        }

        if (positionLabel.getText() != text)
            positionLabel.setText (text, juce::dontSendNotification);
    }
}
