#pragma once

#include "Common.h"

namespace harmonia::spike::audio_device
{
    /** One line per device type with the devices it offers. */
    juce::String listAvailableDevices (te::Engine&);

    /** Type, name, sample rate, buffer size and latency of the open device. */
    juce::String describeCurrentDevice (te::Engine&);

    /** Switches to an ASIO device (Focusrite preferred) if ASIO is available.
        Returns an empty string on success, otherwise the reason. */
    juce::String preferAsio (te::Engine&);
}
