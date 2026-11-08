#include "ui/PositionDisplay.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    PositionDisplay::PositionDisplay()
    {
        setTooltip ("Where the song is: bar . beat . sixteenth, and the time");
    }

    void PositionDisplay::setText (const juce::String& main, const juce::String& secondary)
    {
        if (main != mainText || secondary != secondaryText)
        {
            mainText = main;
            secondaryText = secondary;
            repaint();
        }
    }

    void PositionDisplay::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::mixer::meterBackground);
        g.fillRoundedRectangle (bounds, theme::size::cornerRadius);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (bounds, theme::size::cornerRadius, 1.0f);

        const auto mono = juce::Font::getDefaultMonospacedFontName();
        auto area = getLocalBounds().reduced (theme::size::padding, 0);

        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (mono, theme::size::smallFontSize, juce::Font::plain));
        g.drawText (secondaryText, area.removeFromRight (area.getWidth() * 2 / 5), juce::Justification::centredRight, false);

        g.setColour (theme::colour::textPrimary);
        g.setFont (juce::FontOptions (mono, (float) getHeight() * 0.62f, juce::Font::bold));
        g.drawText (mainText, area, juce::Justification::centredLeft, false);
    }

    juce::String PositionDisplay::barsBeatsText (const te::TempoSequence& tempo, double seconds)
    {
        const auto position = tempo.toBarsAndBeats (te::TimePosition::fromSeconds (seconds));

        // A hair of slack, so that exactly on a beat (give or take rounding) counts as that beat.
        auto bars = position.bars;
        auto beats = position.beats.inBeats() + 1.0e-6;

        if (position.numerator > 0 && beats >= (double) position.numerator)
        {
            ++bars;
            beats -= (double) position.numerator;
        }

        const auto beat = (int) std::floor (beats);
        const auto sixteenth = juce::jlimit (0, 3, (int) std::floor ((beats - beat) * 4.0));
        return juce::String (bars + 1) + "." + juce::String (beat + 1) + "." + juce::String (sixteenth + 1);
    }

    juce::String PositionDisplay::clockText (double seconds)
    {
        const auto negative = seconds < 0.0;
        const auto totalMs = (juce::int64) std::llround (std::abs (seconds) * 1000.0);
        const auto minutes = totalMs / 60000;
        const auto ms = totalMs % 60000;

        return (negative && totalMs > 0 ? "-" : "") + juce::String (minutes) + ":"
             + juce::String (ms / 1000).paddedLeft ('0', 2) + "." + juce::String (ms % 1000).paddedLeft ('0', 3);
    }
}
