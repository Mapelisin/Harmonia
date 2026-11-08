#pragma once

#include "audio/DeviceStatus.h"

namespace harmonia::ui
{
    /** Bottom bar: audio device, audio CPU load and renderer. Refreshes a few times a second. */
    class StatusBar final : public juce::Component,
                            private juce::Timer
    {
    public:
        explicit StatusBar (core::Session&);
        ~StatusBar() override;

        void paint (juce::Graphics&) override;

    private:
        core::Session& session;
        juce::String deviceText, cpuText, rendererText;
        bool deviceOpen = false;

        void timerCallback() override;
        void refresh();
    };
}
