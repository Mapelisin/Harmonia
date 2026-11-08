#pragma once

#include "core/Tracktion.h"

namespace harmonia::ui
{
    /** The transport's counter: where the song is, large in bars, beats and sixteenths, and
        next to it in minutes and seconds. */
    class PositionDisplay final : public juce::Component,
                                  public juce::SettableTooltipClient
    {
    public:
        PositionDisplay();

        /** `main` large ("5.2.3"), `secondary` small beside it ("0:08.250"). */
        void setText (const juce::String& main, const juce::String& secondary);

        const juce::String& getMainText() const noexcept         { return mainText; }
        const juce::String& getSecondaryText() const noexcept    { return secondaryText; }

        void paint (juce::Graphics&) override;

        /** "5.2.3": bar, beat and sixteenth of the beat, each counted from 1 (bar 1 is where the
            song starts; before it, "0.4.1"...). */
        static juce::String barsBeatsText (const te::TempoSequence&, double seconds);

        /** "1:05.250": minutes, seconds and milliseconds ("-0:01.500" before the start). */
        static juce::String clockText (double seconds);

    private:
        juce::String mainText, secondaryText;
    };
}
