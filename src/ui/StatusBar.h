#pragma once

#include "audio/DeviceStatus.h"

namespace harmonia::ui
{
    /** Bottom bar: audio device, an optional warning, audio CPU load and renderer.
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

    private:
        core::Session& session;
        juce::String deviceText, cpuText, rendererText, warningText;
        bool deviceOpen = false;

        void timerCallback() override;
        void refresh();
    };
}
