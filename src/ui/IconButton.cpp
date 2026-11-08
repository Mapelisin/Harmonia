#include "ui/IconButton.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    IconButton::IconButton (const juce::String& name, juce::Path p)
        : juce::Button (name), icon (std::move (p)),
          iconColour (theme::colour::textPrimary), onColour (theme::colour::accent.withAlpha (0.35f))
    {
    }

    void IconButton::setIcon (juce::Path p)
    {
        icon = std::move (p);
        repaint();
    }

    void IconButton::setIconColour (juce::Colour c)
    {
        if (c != iconColour)
        {
            iconColour = c;
            repaint();
        }
    }

    void IconButton::setOnColour (juce::Colour c)
    {
        if (c != onColour)
        {
            onColour = c;
            repaint();
        }
    }

    void IconButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
    {
        getLookAndFeel().drawButtonBackground (g, *this, getToggleState() ? onColour : theme::colour::surfaceRaised,
                                               isHighlighted, isDown);

        const auto area = getLocalBounds().toFloat().reduced ((float) getHeight() * 0.24f);
        icons::draw (g, icon, area, isEnabled() ? iconColour : iconColour.withMultipliedAlpha (0.4f));
    }
}
