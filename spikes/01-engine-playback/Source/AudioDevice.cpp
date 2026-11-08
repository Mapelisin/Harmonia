#include "AudioDevice.h"

namespace harmonia::spike::audio_device
{
    juce::String listAvailableDevices (te::Engine& engine)
    {
        juce::String result;

        for (auto* type : engine.getDeviceManager().deviceManager.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            result << type->getTypeName() << ": "
                   << type->getDeviceNames (false).joinIntoString (", ") << "\n";
        }

        return result;
    }

    juce::String describeCurrentDevice (te::Engine& engine)
    {
        auto& adm = engine.getDeviceManager().deviceManager;
        auto* device = adm.getCurrentAudioDevice();

        if (device == nullptr)
            return "No audio device open";

        const auto sampleRate = device->getCurrentSampleRate();
        const auto bufferSize = device->getCurrentBufferSizeSamples();
        const auto outputLatencyMs = 1000.0 * (device->getOutputLatencyInSamples() + bufferSize) / sampleRate;

        return device->getTypeName() + " / " + device->getName()
             + " | " + juce::String (sampleRate, 0) + " Hz"
             + " | buffer " + juce::String (bufferSize)
             + " (" + juce::String (1000.0 * bufferSize / sampleRate, 1) + " ms)"
             + " | output latency ~" + juce::String (outputLatencyMs, 1) + " ms";
    }

    juce::String preferAsio (te::Engine& engine)
    {
        auto& adm = engine.getDeviceManager().deviceManager;

        juce::AudioIODeviceType* asioType = nullptr;

        for (auto* type : adm.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
                asioType = type;

        if (asioType == nullptr)
            return "ASIO device type is not available";

        asioType->scanForDevices();
        const auto names = asioType->getDeviceNames (false);

        if (names.isEmpty())
            return "No ASIO drivers installed";

        auto chosen = names[0];

        for (auto& n : names)
            if (n.containsIgnoreCase ("Focusrite USB"))
                chosen = n;

        const auto previousType = adm.getCurrentAudioDeviceType();
        const auto previousSetup = adm.getAudioDeviceSetup();

        adm.setCurrentAudioDeviceType ("ASIO", true);

        // The driver may refuse a sample rate that another app (e.g. a browser via
        // Windows Audio) has locked the interface to, so try the common rates.
        juce::StringArray errors;

        for (double rate : { 0.0, 48000.0, 44100.0, 96000.0 })
        {
            auto setup = adm.getAudioDeviceSetup();
            setup.outputDeviceName = chosen;
            setup.inputDeviceName = chosen;
            setup.useDefaultInputChannels = true;
            setup.useDefaultOutputChannels = true;
            setup.sampleRate = rate;
            setup.bufferSize = 0;

            const auto error = adm.setAudioDeviceSetup (setup, true);

            if (error.isEmpty() && adm.getCurrentAudioDevice() != nullptr)
                return {};

            errors.add (chosen + " @ " + (rate > 0 ? juce::String (rate, 0) : juce::String ("default")) + " Hz: " + error);
        }

        // Fall back to whatever worked before so we still have sound.
        adm.setCurrentAudioDeviceType (previousType, true);
        adm.setAudioDeviceSetup (previousSetup, true);

        if (adm.getCurrentAudioDevice() == nullptr)
            adm.initialiseWithDefaultDevices (0, 2);

        return errors.joinIntoString ("; ");
    }
}
