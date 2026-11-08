#include "ui/Theme.h"

namespace harmonia::ui::theme
{
    LookAndFeel::LookAndFeel()
    {
        setColourScheme ({ colour::background,      // windowBackground
                           colour::surface,         // widgetBackground
                           colour::surface,         // menuBackground
                           colour::border,          // outline
                           colour::textPrimary,     // defaultText
                           colour::surfaceRaised,   // defaultFill
                           colour::textPrimary,     // highlightedText
                           colour::accent,          // highlightedFill
                           colour::textPrimary });  // menuText

        setColour (juce::ResizableWindow::backgroundColourId, colour::background);
        setColour (juce::TextButton::buttonColourId, colour::surfaceRaised);
        setColour (juce::TextButton::textColourOffId, colour::textPrimary);
        setColour (juce::Label::textColourId, colour::textPrimary);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool isMouseOverButton, bool isButtonDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = backgroundColour;

        if (isButtonDown)
            fill = fill.darker (0.2f);
        else if (isMouseOverButton)
            fill = fill.brighter (0.08f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, size::cornerRadius);
        g.setColour (colour::border);
        g.drawRoundedRectangle (bounds, size::cornerRadius, 1.0f);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return juce::Font (juce::FontOptions (size::fontSize));
    }
}
