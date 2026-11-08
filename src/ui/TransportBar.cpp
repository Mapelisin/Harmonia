#include "ui/TransportBar.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    TransportBar::TransportBar (audio::Transport& t, te::Edit& e)
        : transport (t), edit (e)
    {
        playButton.setClickingTogglesState (false);
        playButton.onClick = [this]
        {
            transport.togglePlayStop();
            refresh();
        };

        positionLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        positionLabel.setFont (juce::FontOptions (theme::size::fontSize));
        positionLabel.setJustificationType (juce::Justification::centredLeft);

        addAndMakeVisible (playButton);
        addAndMakeVisible (positionLabel);

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
        playButton.setBounds (area.removeFromLeft (70).withSizeKeepingCentre (70, theme::size::buttonHeight));
        area.removeFromLeft (theme::size::padding);
        positionLabel.setBounds (area);
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

        const auto seconds = transport.getPositionSeconds();
        const auto barsAndBeats = edit.tempoSequence.toBarsAndBeats (te::TimePosition::fromSeconds (seconds));

        const auto text = juce::String (barsAndBeats.bars + 1) + "." + juce::String (barsAndBeats.getWholeBeats() + 1)
                        + "   " + juce::String (seconds, 2) + " s";

        if (positionLabel.getText() != text)
            positionLabel.setText (text, juce::dontSendNotification);
    }
}
