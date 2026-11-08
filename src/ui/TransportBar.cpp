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

        recordButton.setTooltip ("Record (R): notes played while the song plays go into the clip open in the piano roll");
        recordButton.onClick = [this] { noteInput.setRecordArmed (! noteInput.isRecordArmed()); };

        clickButton.setTooltip ("Metronome (Ctrl+M)");
        clickButton.onClick = [this]
        {
            edit.clickTrackEnabled = ! edit.clickTrackEnabled.get();
            refresh();
        };

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

        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &recordButton, &clickButton, &modeButton, &positionLabel,
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

    void TransportBar::refresh()
    {
        const auto active = transport.isPlayRequested() || transport.isPlaying();
        playButton.setButtonText (active ? "Stop" : "Play");
        playButton.setColour (juce::TextButton::buttonColourId,
                              active ? theme::colour::accent.withAlpha (0.35f) : theme::colour::surfaceRaised);

        const auto recordColour = noteInput.isRecording() ? theme::colour::record
                                : noteInput.isRecordArmed() ? theme::colour::record.withAlpha (0.45f)
                                                            : theme::colour::surfaceRaised;
        recordButton.setColour (juce::TextButton::buttonColourId, recordColour);

        clickButton.setColour (juce::TextButton::buttonColourId,
                               edit.clickTrackEnabled.get() ? theme::colour::accent.withAlpha (0.35f) : theme::colour::surfaceRaised);

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

        const auto text = juce::String (barsAndBeats.bars + 1) + "." + juce::String (barsAndBeats.getWholeBeats() + 1)
                        + "   " + juce::String (seconds, 2) + " s";

        if (positionLabel.getText() != text)
            positionLabel.setText (text, juce::dontSendNotification);
    }
}
