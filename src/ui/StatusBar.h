#pragma once

#include "audio/DeviceStatus.h"

namespace harmonia::ui
{
    /** Bottom bar: audio device, an optional warning or message, MIDI keyboards and renderer
        (the audio load is in the menu row).
        Refreshes a few times a second. */
    class StatusBar final : public juce::Component,
                            private juce::Timer
    {
    public:
        explicit StatusBar (core::Session&);
        ~StatusBar() override;

        void paint (juce::Graphics&) override;

        /** A problem the user should know about (e.g. the chosen audio interface didn't start).
            Shown until replaced; an empty string clears it. */
        void setWarning (const juce::String&);

        /** Which MIDI keyboards are connected, e.g. "MIDI: KeyLab 61". */
        void setMidiText (const juce::String&);

        /** A short note that something worked (e.g. "Saved"); disappears after a few seconds. */
        void setMessage (const juce::String&);

    private:
        core::Session& session;
        juce::String deviceText, rendererText, warningText, midiText, messageText;
        juce::uint32 messageTime = 0;
        bool deviceOpen = false;

        void timerCallback() override;
        void refresh();
    };
}
