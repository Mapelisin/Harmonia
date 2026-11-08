#pragma once

#include "audio/Transport.h"
#include "ui/NoteInput.h"

namespace harmonia::ui
{
    /** Play, record, metronome, the position (bar.beat and time) and the note input settings:
        the computer keyboard piano and how recorded notes are quantized. */
    class TransportBar final : public juce::Component,
                               private juce::Timer
    {
    public:
        TransportBar (audio::Transport&, te::Edit&, NoteInput&);
        ~TransportBar() override;

        void resized() override;

        /** Re-reads everything shown (also runs on a timer). */
        void refresh();

    private:
        audio::Transport& transport;
        te::Edit& edit;
        NoteInput& noteInput;

        juce::TextButton playButton { "Play" }, recordButton { "Rec" }, clickButton { "Click" }, keysButton;
        juce::Label positionLabel;
        juce::ComboBox quantizeBox;

        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (TransportBar)
    };
}
