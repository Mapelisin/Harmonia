#include <doctest.h>

#include "TestSession.h"
#include "audio/DeviceStatus.h"

TEST_SUITE ("audio::DeviceStatus")
{
    TEST_CASE ("a headless session reports no device")
    {
        const auto status = harmonia::audio::getDeviceStatus (harmonia::test::session());

        CHECK_FALSE (status.isOpen);
        CHECK (status.toDisplayString() == "No audio device");
    }

    TEST_CASE ("an open device is described with type, name, rate, buffer and latency")
    {
        harmonia::audio::DeviceStatus status;
        status.isOpen = true;
        status.typeName = "ASIO";
        status.deviceName = "Focusrite USB ASIO";
        status.sampleRate = 48000.0;
        status.bufferSize = 256;
        status.outputLatencyMs = 7.1;

        const auto text = status.toDisplayString();

        CHECK (text.contains ("ASIO"));
        CHECK (text.contains ("Focusrite USB ASIO"));
        CHECK (text.contains ("48.0 kHz"));
        CHECK (text.contains ("256 smp"));
        CHECK (text.contains ("7.1 ms"));
    }
}
