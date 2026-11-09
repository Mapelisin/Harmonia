#include "ui/settings/AudioSettingsTab.h"
#include "audio/DeviceStatus.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    AudioSettingsTab::AudioSettingsTab (core::Session& s)
        : session (s)
    {
        latency.setFont (juce::FontOptions (theme::size::smallFontSize));
        latency.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        latency.setJustificationType (juce::Justification::topLeft);
        latency.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (latency);

        if (! session.devicesEnabled())
        {
            latency.setText ("Harmonia was started without sound devices.", juce::dontSendNotification);
            return;
        }

        // Two outputs and up to two inputs (a stereo pair or two mono ones), as before.
        selector = std::make_unique<juce::AudioDeviceSelectorComponent> (audio::getDeviceManager (session),
                                                                         0, 2, 1, 2, false, false, true, false);
        addAndMakeVisible (*selector);
        audio::getDeviceManager (session).addChangeListener (this);
        updateLatency();
    }

    AudioSettingsTab::~AudioSettingsTab()
    {
        if (selector != nullptr)
            audio::getDeviceManager (session).removeChangeListener (this);
    }

    juce::String AudioSettingsTab::describeLatency (int bufferSamples, int outputLatencySamples, double sampleRate)
    {
        if (sampleRate <= 0.0 || bufferSamples <= 0)
            return "No sound device is open: choose one above.";

        // The latency a driver reports already holds its buffer (ASIO, WASAPI); some report none.
        const auto samples = juce::jmax (bufferSamples, outputLatencySamples);
        const auto ms = samples * 1000.0 / sampleRate;

        return "From a key to the speakers: about " + juce::String (ms, 1) + " ms (a buffer of " + juce::String (bufferSamples)
               + " samples at " + juce::String (juce::roundToInt (sampleRate)) + " Hz).\n"
               + (ms <= 12.0 ? "Playing feels instant. If the sound crackles, take a larger buffer."
                             : "A keyboard will feel a little late: a smaller buffer is tighter (if it doesn't crackle).");
    }

    void AudioSettingsTab::updateLatency()
    {
        auto* device = audio::getDeviceManager (session).getCurrentAudioDevice();

        latency.setText (device != nullptr ? describeLatency (device->getCurrentBufferSizeSamples(), device->getOutputLatencyInSamples(),
                                                              device->getCurrentSampleRate())
                                           : describeLatency (0, 0, 0.0),
                         juce::dontSendNotification);
    }

    void AudioSettingsTab::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateLatency();
    }

    void AudioSettingsTab::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
    }

    void AudioSettingsTab::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding);

        if (selector == nullptr)
        {
            latency.setBounds (area.reduced (theme::size::padding).removeFromTop (40));
            return;
        }

        latency.setBounds (area.removeFromBottom (40));
        selector->setBounds (area);
    }
}
