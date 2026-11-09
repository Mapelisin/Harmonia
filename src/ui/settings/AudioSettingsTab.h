#pragma once

#include "core/Session.h"

namespace harmonia::ui
{
    /** The sound card: which device, its rate and buffer (JUCE's own selector), and a line on
        the latency that buffer makes. */
    class AudioSettingsTab final : public juce::Component,
                                   private juce::ChangeListener
    {
    public:
        explicit AudioSettingsTab (core::Session&);
        ~AudioSettingsTab() override;

        /** What the line under the selector says now. */
        juce::String getLatencyText() const    { return latency.getText(); }

        /** "From a key to the speakers: about 10.7 ms..." for a device with this buffer, the
            output latency it reports (both in samples) and rate; no rate: no device. */
        static juce::String describeLatency (int bufferSamples, int outputLatencySamples, double sampleRate);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        core::Session& session;
        std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
        juce::Label latency;

        void updateLatency();
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        JUCE_DECLARE_NON_COPYABLE (AudioSettingsTab)
    };
}
