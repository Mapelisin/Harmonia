#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Call once, right after the Session is created.

        Tracktion reopens the device the user chose last time. If that fails (interface
        unplugged, ASIO driver that does not start, ...) JUCE silently falls back to the
        system's default output. This compares the remembered choice with what actually
        opened and returns a message for the user when they differ, or an empty string. */
    juce::String checkStartupDevice (core::Session&);
}
