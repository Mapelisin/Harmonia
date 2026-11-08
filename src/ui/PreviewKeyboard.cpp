#include "ui/PreviewKeyboard.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int lowestKey = 24;    // C1
        constexpr int highestKey = 108;  // C8

        int countWhiteKeys()
        {
            int n = 0;

            for (int note = lowestKey; note <= highestKey; ++note)
                n += juce::MidiMessage::isMidiNoteBlack (note) ? 0 : 1;

            return n;
        }
    }

    PreviewKeyboard::PreviewKeyboard (audio::NotePreview& p)
        : preview (p)
    {
        keyboard.setAvailableRange (lowestKey, highestKey);
        keyboard.setScrollButtonsVisible (false);
        keyboard.setOctaveForMiddleC (4);
        keyboard.setKeyPressBaseOctave (5);
        keyboard.setVelocity (0.8f, true);
        keyboard.setWantsKeyboardFocus (true);

        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::colour::accent);
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                            theme::colour::accent.withAlpha (0.3f));
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, theme::colour::border);
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, theme::colour::surface);
        keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, theme::colour::textSecondary);

        state.addListener (this);
        addAndMakeVisible (keyboard);
    }

    PreviewKeyboard::~PreviewKeyboard()
    {
        state.removeListener (this);
        preview.allNotesOff();
    }

    void PreviewKeyboard::resized()
    {
        // The whole range always fits: keys get wider or narrower with the window.
        keyboard.setKeyWidth ((float) getWidth() / (float) countWhiteKeys());
        keyboard.setBounds (getLocalBounds());
    }

    void PreviewKeyboard::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
    {
        preview.noteOn (note, juce::roundToInt (velocity * 127.0f));
    }

    void PreviewKeyboard::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
    {
        preview.noteOff (note);
    }
}
