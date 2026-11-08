#include "audio/DeviceSetup.h"
#include "audio/DeviceStatus.h"

namespace harmonia::audio
{
    juce::String checkStartupDevice (core::Session& session)
    {
        if (! session.devicesEnabled())
            return {};

        auto& engine = session.getEngine();
        auto* device = getDeviceManager (session).getCurrentAudioDevice();

        // The remembered choice is only overwritten asynchronously, after this point.
        const auto saved = engine.getPropertyStorage().getXmlProperty (te::SettingID::audio_device_setup);

        if (saved == nullptr)
            return device == nullptr ? "No audio output could be opened" : juce::String();

        const auto wantedType = saved->getStringAttribute ("deviceType");
        const auto wantedOutput = saved->getStringAttribute ("audioOutputDeviceName");

        if (wantedOutput.isEmpty())
            return device == nullptr ? "No audio output could be opened" : juce::String();

        if (device != nullptr && device->getTypeName() == wantedType && device->getName() == wantedOutput)
            return {};

        auto message = "Couldn't start " + wantedOutput + " (" + wantedType + ")";

        if (device != nullptr)
            return message + ", using " + device->getName() + " (" + device->getTypeName() + ") instead";

        return message + ", no audio output";
    }
}
