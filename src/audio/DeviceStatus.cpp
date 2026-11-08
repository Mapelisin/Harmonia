#include "audio/DeviceStatus.h"

namespace harmonia::audio
{
    juce::String DeviceStatus::toDisplayString() const
    {
        if (! isOpen)
            return "No audio device";

        return typeName + juce::String::fromUTF8 (" \xc2\xb7 ") + deviceName
             + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (sampleRate / 1000.0, 1) + " kHz"
             + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (bufferSize) + " smp"
             + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (outputLatencyMs, 1) + " ms";
    }

    juce::AudioDeviceManager& getDeviceManager (core::Session& session)
    {
        return session.getEngine().getDeviceManager().deviceManager;
    }

    DeviceStatus getDeviceStatus (core::Session& session)
    {
        DeviceStatus status;

        if (! session.devicesEnabled())
            return status;

        auto* device = getDeviceManager (session).getCurrentAudioDevice();

        if (device == nullptr)
            return status;

        status.isOpen = true;
        status.typeName = device->getTypeName();
        status.deviceName = device->getName();
        status.sampleRate = device->getCurrentSampleRate();
        status.bufferSize = device->getCurrentBufferSizeSamples();

        if (status.sampleRate > 0.0)
            status.outputLatencyMs = 1000.0 * (device->getOutputLatencyInSamples() + status.bufferSize)
                                   / status.sampleRate;

        return status;
    }

    float getAudioCpuLoad (core::Session& session)
    {
        return session.getEngine().getDeviceManager().getCpuUsage();
    }
}
