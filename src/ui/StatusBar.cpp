#include "ui/StatusBar.h"
#include "ui/RenderingPolicy.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    StatusBar::StatusBar (core::Session& s)
        : session (s)
    {
        refresh();
        startTimerHz (4);
    }

    StatusBar::~StatusBar()
    {
        stopTimer();
    }

    void StatusBar::timerCallback()
    {
        refresh();
    }

    void StatusBar::refresh()
    {
        const auto status = audio::getDeviceStatus (session);
        deviceOpen = status.isOpen;
        deviceText = status.toDisplayString();
        // juce::String (float, 0) prints full precision, so round explicitly.
        cpuText = deviceOpen ? "Audio CPU " + juce::String (juce::roundToInt (audio::getAudioCpuLoad (session) * 100.0f)) + "%"
                             : juce::String();

        if (auto* top = getTopLevelComponent())
            rendererText = getCurrentRendererName (*top);

        repaint();
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::surface);
        g.setColour (theme::colour::border);
        g.fillRect (0, 0, getWidth(), 1);

        auto area = getLocalBounds().reduced (theme::size::padding, 0);
        g.setFont (juce::FontOptions (theme::size::smallFontSize));

        g.setColour (theme::colour::textSecondary);
        g.drawText (rendererText, area.removeFromRight (160), juce::Justification::centredRight, true);
        g.drawText (cpuText, area.removeFromRight (110), juce::Justification::centredRight, true);

        g.setColour (deviceOpen ? theme::colour::textSecondary : theme::colour::warning);
        g.drawText (deviceText, area, juce::Justification::centredLeft, true);
    }
}
