#include "ui/CpuMeter.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    CpuMeter::CpuMeter (std::function<std::optional<float>()> read)
        : readLoad (std::move (read))
    {
        setTooltip ("Audio load: how much of the time between two audio buffers working out the sound takes.\n"
                    "Near 100% it starts to crackle: a larger buffer in File > Settings > Audio, or fewer heavy plugins.\n"
                    "Click: which plugin takes how much.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        refresh();
        startTimerHz (4);
    }

    CpuMeter::~CpuMeter()
    {
        stopTimer();
    }

    void CpuMeter::timerCallback()
    {
        refresh();
    }

    void CpuMeter::refresh()
    {
        auto now = readLoad != nullptr ? readLoad() : std::nullopt;

        if (now.has_value())
            now = juce::jlimit (0.0f, 1.0f, *now);

        // Shown in whole percent: repaint only when that changes.
        const auto percent = [] (std::optional<float> v) { return v.has_value() ? juce::roundToInt (*v * 100.0f) : -1; };

        if (percent (now) != percent (load))
        {
            load = now;
            repaint();
        }
    }

    void CpuMeter::mouseUp (const juce::MouseEvent& event)
    {
        if (onClick != nullptr && ! event.mods.isPopupMenu() && getLocalBounds().contains (event.getPosition()))
            onClick();
    }

    juce::Colour CpuMeter::getColour (float value)
    {
        return value < 0.6f ? theme::mixer::meterLow
             : value < 0.85f ? theme::mixer::meterMid
                             : theme::colour::record;
    }

    void CpuMeter::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();
        g.setFont (juce::FontOptions (theme::size::smallFontSize));
        g.setColour (theme::colour::textSecondary);
        g.drawText ("CPU", area.removeFromLeft (30), juce::Justification::centredLeft, false);

        auto text = area.removeFromRight (36);
        auto bar = area.withSizeKeepingCentre (area.getWidth(), 8).toFloat();
        g.setColour (theme::mixer::meterBackground);
        g.fillRoundedRectangle (bar, 2.0f);

        if (load.has_value())
        {
            g.setColour (getColour (*load));
            g.fillRoundedRectangle (bar.withWidth (std::max (2.0f, bar.getWidth() * *load)), 2.0f);
        }

        g.setColour (theme::colour::textSecondary);
        g.drawText (load.has_value() ? juce::String (juce::roundToInt (*load * 100.0f)) + "%" : juce::String ("-"),
                    text, juce::Justification::centredRight, false);
    }
}
