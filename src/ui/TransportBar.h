#pragma once

#include "audio/Transport.h"
#include "core/Tempo.h"
#include "ui/DragNumber.h"
#include "ui/IconButton.h"
#include "ui/Icons.h"
#include "ui/NoteInput.h"
#include "ui/PositionDisplay.h"
#include "ui/arrangement/ArrangementModel.h"

namespace harmonia::ui
{
    /** Back to the start, stop, play, record, the counter (bar.beat.sixteenth and time), the
        tempo (drag, type or tap it) and the time signature, metronome, count-in, what Play plays
        (clip or song), and the note input settings: the computer keyboard piano and how recorded
        notes are quantized. */
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

        /** Back to the start (the button, or Stop while stopped). */
        std::function<void()> onGoToStart;

        /** The width that shows everything; narrower, the quantize choice shrinks first. */
        static int getPreferredWidth();

        IconButton& getPlayButton() noexcept            { return playButton; }
        IconButton& getStopButton() noexcept            { return stopButton; }
        IconButton& getRecordButton() noexcept          { return recordButton; }
        IconButton& getClickButton() noexcept           { return clickButton; }
        PositionDisplay& getPositionDisplay() noexcept  { return positionDisplay; }

        DragNumber& getTempoBox() noexcept              { return tempoBox; }

        /** A tap of the Tap button at `seconds`: from the second tap on, sets the tempo. */
        void tap (double seconds);

        /** Time signature n/4 (one undo step). */
        void setBeatsPerBar (int);

    private:
        audio::Transport& transport;
        te::Edit& edit;
        NoteInput& noteInput;
        arrangement::ArrangementModel& arrangementModel;

        IconButton toStartButton { "To start", icons::toStart() }, stopButton { "Stop", icons::stop() },
                   playButton { "Play", icons::play() }, recordButton { "Record", icons::record() },
                   clickButton { "Metronome", icons::metronome() };
        juce::TextButton countInButton { "Count-in" }, modeButton, keysButton;
        PositionDisplay positionDisplay;
        juce::ComboBox quantizeBox;
        DragNumber tempoBox;
        juce::TextButton timeSigButton, tapButton { "Tap" };
        core::tempo::TapTempo tapTempo;

        void timerCallback() override;
        void showCountInMenu();
        void showTimeSigMenu();

        JUCE_DECLARE_NON_COPYABLE (TransportBar)
    };
}
