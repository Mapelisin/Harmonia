#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Design tokens: every colour and size used by the UI comes from here,
    so the look can be tuned in one place. Strict, flat, dense — no gradients,
    no large radii, no decorative cards. */
namespace harmonia::ui::theme
{
    namespace colour
    {
        inline const juce::Colour background    { 0xff1a1c20 };
        inline const juce::Colour surface       { 0xff212429 };
        inline const juce::Colour surfaceRaised { 0xff2a2e34 };
        inline const juce::Colour border        { 0xff353a42 };
        inline const juce::Colour textPrimary   { 0xffd8dbe0 };
        inline const juce::Colour textSecondary { 0xff8a919b };
        inline const juce::Colour accent        { 0xff4f9fe0 };
        inline const juce::Colour warning       { 0xffe0a64f };
    }

    namespace size
    {
        inline constexpr int headerHeight = 34;
        inline constexpr int statusBarHeight = 24;
        inline constexpr int padding = 8;
        inline constexpr int buttonHeight = 22;
        inline constexpr float cornerRadius = 2.0f;
        inline constexpr float fontSize = 13.0f;
        inline constexpr float smallFontSize = 12.0f;
    }

    /** Applies the tokens to JUCE's standard widgets. */
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    };
}
