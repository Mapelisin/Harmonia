#pragma once

#include "audio/Transport.h"

namespace harmonia::ui
{
    /** Play/stop button and the playback position (bar.beat and time). */
    class TransportBar final : public juce::Component,
                               private juce::Timer
    {
    public:
        TransportBar (audio::Transport&, te::Edit&);
        ~TransportBar() override;

        void resized() override;

    private:
        audio::Transport& transport;
        te::Edit& edit;

        juce::TextButton playButton { "Play" };
        juce::Label positionLabel;

        void timerCallback() override;
        void refresh();

        JUCE_DECLARE_NON_COPYABLE (TransportBar)
    };
}
