#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** A snapshot of the audio output device, for display. */
    struct DeviceStatus
    {
        bool isOpen = false;
        juce::String typeName;     // e.g. "ASIO", "Windows Audio"
        juce::String deviceName;
        double sampleRate = 0.0;
        int bufferSize = 0;
        double outputLatencyMs = 0.0;

        /** One line for a status bar, e.g. "ASIO · Focusrite USB ASIO · 48 kHz · 256 smp · 7.1 ms". */
        juce::String toDisplayString() const;
    };

    DeviceStatus getDeviceStatus (core::Session&);

    /** Proportion of the audio callback's time budget currently used (0..1). */
    float getAudioCpuLoad (core::Session&);

    /** The device manager behind the engine, for the device settings dialog. */
    juce::AudioDeviceManager& getDeviceManager (core::Session&);
}
