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

        if (auto* top = getTopLevelComponent())
            rendererText = getCurrentRendererName (*top);

        if (messageText.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 8000)
            messageText.clear();

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
        g.drawText (midiText, area.removeFromRight (260), juce::Justification::centredRight, true);

        // What just happened comes first; a warning that still stands is back when the message goes.
        if (messageText.isNotEmpty())
        {
            g.setColour (theme::colour::textPrimary);
            g.drawText (messageText, area.removeFromRight (area.getWidth() / 2), juce::Justification::centredRight, true);
        }
        else if (warningText.isNotEmpty())
        {
            g.setColour (theme::colour::warning);
            g.drawText (warningText, area.removeFromRight (area.getWidth() / 2), juce::Justification::centredRight, true);
        }

        g.setColour (deviceOpen ? theme::colour::textSecondary : theme::colour::warning);
        g.drawText (deviceText, area, juce::Justification::centredLeft, true);
    }

    void StatusBar::setMidiText (const juce::String& text)
    {
        midiText = text;
        repaint();
    }

    void StatusBar::setMessage (const juce::String& text)
    {
        messageText = text;
        messageTime = juce::Time::getMillisecondCounter();
        repaint();
    }

    void StatusBar::setWarning (const juce::String& text)
    {
        warningText = text;
        repaint();
    }
}
