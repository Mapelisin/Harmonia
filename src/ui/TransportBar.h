#pragma once

#include "audio/Transport.h"
#include "ui/NoteInput.h"
#include "ui/arrangement/ArrangementModel.h"

namespace harmonia::ui
{
    /** Play, record, metronome, what Play plays (clip or song), the position (bar.beat and
        time) and the note input settings: the computer keyboard piano and how recorded notes
        are quantized. */
    class TransportBar final : public juce::Component,
                               private juce::Timer
    {
    public:
        TransportBar (audio::Transport&, te::Edit&, NoteInput&, arrangement::ArrangementModel&);
        ~TransportBar() override;

        void resized() override;

        /** Re-reads everything shown (also runs on a timer). */
        void refresh();

        /** The Clip / Song button was clicked. */
        std::function<void()> onPlayModeToggled;

    private:
        audio::Transport& transport;
        te::Edit& edit;
        NoteInput& noteInput;
        arrangement::ArrangementModel& arrangementModel;

        juce::TextButton playButton { "Play" }, recordButton { "Rec" }, clickButton { "Click" }, countInButton { "Count-in" },
                         modeButton, keysButton;
        juce::Label positionLabel;
        juce::ComboBox quantizeBox;

        void timerCallback() override;
        void showCountInMenu();

        JUCE_DECLARE_NON_COPYABLE (TransportBar)
    };
}
