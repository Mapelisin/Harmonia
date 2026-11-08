#include "ui/Theme.h"

namespace harmonia::ui::theme
{
    namespace pianoroll
    {
        juce::Colour getNoteColour (int colourGroup)
        {
            static const juce::Colour palette[] = {
                juce::Colour (0xff5aa9e6), juce::Colour (0xff62c79a), juce::Colour (0xffd9c45a), juce::Colour (0xffe0875a),
                juce::Colour (0xffd9677a), juce::Colour (0xffb17ad9), juce::Colour (0xff6f8be0), juce::Colour (0xff4fc2c9),
                juce::Colour (0xff9ccf5c), juce::Colour (0xffe6a85a), juce::Colour (0xffe06f9f), juce::Colour (0xff8d95a8),
                juce::Colour (0xff3d8fc4), juce::Colour (0xff42a77a), juce::Colour (0xffb8a24a), juce::Colour (0xffc46a4a)
            };

            return palette[juce::jlimit (0, 15, colourGroup)];
        }
    }

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
