#pragma once

#include "audio/NotePreview.h"

namespace harmonia::ui
{
    /** An on-screen piano that plays the selected track's instrument. Click the keys, or
        click it once and play with the computer keyboard (A W S E D F T G Y H U J K). */
    class PreviewKeyboard final : public juce::Component,
                                  private juce::MidiKeyboardState::Listener
    {
    public:
        explicit PreviewKeyboard (audio::NotePreview&);
        ~PreviewKeyboard() override;

        void resized() override;

    private:
        audio::NotePreview& preview;
        juce::MidiKeyboardState state;
        juce::MidiKeyboardComponent keyboard { state, juce::MidiKeyboardComponent::horizontalKeyboard };

        void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
        void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;

        JUCE_DECLARE_NON_COPYABLE (PreviewKeyboard)
    };
}
