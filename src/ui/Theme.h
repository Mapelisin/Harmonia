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

    /** Piano roll colours. Notes use a 16-colour palette (one per note colour group / MIDI channel). */
    namespace pianoroll
    {
        inline const juce::Colour whiteRow        { 0xff202328 };
        inline const juce::Colour blackRow        { 0xff1a1c20 };
        inline const juce::Colour octaveLine      { 0xff30353d };
        inline const juce::Colour barLine         { 0xff4a515b };
        inline const juce::Colour beatLine        { 0xff32373f };
        inline const juce::Colour stepLine        { 0xff282c32 };
        inline const juce::Colour outsideClip     { 0x70000000 };
        inline const juce::Colour noteBorder      { 0xff111316 };
        inline const juce::Colour noteSelected    { 0xfff2b65c };
        inline const juce::Colour noteMuted       { 0xff4b515a };
        inline const juce::Colour noteLabel       { 0xe0101215 };
        inline const juce::Colour ghostNote       { 0x2cffffff };
        inline const juce::Colour playhead        { 0xffeaeaea };
        inline const juce::Colour selectionFill   { 0x264f9fe0 };
        inline const juce::Colour timeSelection   { 0x1c4f9fe0 };
        inline const juce::Colour sliceLine       { 0xffff6b6b };
        inline const juce::Colour whiteKey        { 0xffd9dce1 };
        inline const juce::Colour whiteKeyText    { 0xff6b717a };
        inline const juce::Colour blackKey        { 0xff23262b };
        inline const juce::Colour clipRange       { 0xff3f6f99 };

        /** Fill colour of a note in colour group 0..15. */
        juce::Colour getNoteColour (int colourGroup);
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
