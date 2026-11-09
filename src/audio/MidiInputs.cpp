#include "audio/MidiInputs.h"

namespace harmonia::audio
{
    MidiInputs::MidiInputs (te::Edit& e)
        : edit (e), devices (e.engine.getDeviceManager())
    {
        devices.addChangeListener (this);
        refreshDevices();
    }

    MidiInputs::~MidiInputs()
    {
        devices.removeChangeListener (this);

        for (auto& device : listening)
            device->removeListener (this);

        cancelPendingUpdate();
    }

    void MidiInputs::setTrack (te::AudioTrack* newTrack)
    {
        track = newTrack;
        routeToTrack();
    }

    juce::StringArray MidiInputs::getDeviceNames() const
    {
        juce::StringArray names;

        for (auto& device : listening)
            names.add (device->getName());

        return names;
    }

    std::vector<MidiInputs::Keyboard> MidiInputs::getKeyboards (te::Engine& engine)
    {
        std::vector<Keyboard> keyboards;

        for (auto& device : engine.getDeviceManager().getMidiInDevices())
            if (std::dynamic_pointer_cast<te::PhysicalMidiInputDevice> (device) != nullptr)
                keyboards.push_back ({ device->getDeviceID(), device->getName(), device->isEnabled() });

        return keyboards;
    }

    void MidiInputs::setKeyboardOn (te::Engine& engine, const juce::String& id, bool on)
    {
        // The engine then lists its inputs again, and every MidiInputs follows (changeListenerCallback).
        for (auto& device : engine.getDeviceManager().getMidiInDevices())
            if (device->getDeviceID() == id && std::dynamic_pointer_cast<te::PhysicalMidiInputDevice> (device) != nullptr)
                device->setEnabled (on);
    }

    void MidiInputs::refreshDevices()
    {
        for (auto& device : listening)
            device->removeListener (this);

        listening.clear();

        for (auto& device : devices.getMidiInDevices())
        {
            if (! device->isEnabled())
                continue;

            if (auto physical = std::dynamic_pointer_cast<te::PhysicalMidiInputDevice> (device))
            {
                physical->addListener (this);
                listening.push_back (physical);
            }
        }

        routeToTrack();
    }

    void MidiInputs::routeToTrack()
    {
        auto* target = dynamic_cast<te::AudioTrack*> (track.get());

        for (auto* instance : edit.getAllInputDevices())
        {
            auto& device = instance->getInputDevice();

            if (device.getDeviceType() != te::InputDevice::physicalMidiDevice)
                continue;

            // Heard all the time, not only while Tracktion's own recording is armed.
            if (device.getMonitorMode() != te::InputDevice::MonitorMode::on)
                device.setMonitorMode (te::InputDevice::MonitorMode::on);

            const auto targets = instance->getTargets();

            if (target == nullptr)
            {
                for (auto id : targets)
                    [[maybe_unused]] auto result = instance->removeTarget (id, nullptr);

                continue;
            }

            if (targets.size() == 1 && targets.getFirst() == target->itemID)
                continue;

            // Harmonia records the notes itself, so the engine's recording stays off.
            if (instance->setTarget (target->itemID, true, nullptr).has_value())
                instance->setRecordingEnabled (target->itemID, false);
        }
    }

    //==============================================================================
    void MidiInputs::handleIncomingMidiMessage (const juce::MidiMessage& message)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (! (message.isNoteOnOrOff() || message.isSustainPedalOn() || message.isSustainPedalOff()))
            return;

        {
            const juce::ScopedLock sl (queueLock);
            queue.push_back ({ message, now });
        }

        triggerAsyncUpdate();
    }

    void MidiInputs::handleAsyncUpdate()
    {
        {
            const juce::ScopedLock sl (queueLock);
            std::swap (queue, delivering);
        }

        for (const auto& r : delivering)
            if (onMessage != nullptr)
                onMessage (r.message, r.wallMs);

        delivering.clear();
    }

    void MidiInputs::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // The engine rebuilds its inputs first; route the new ones afterwards.
        juce::MessageManager::callAsync ([ref = juce::WeakReference<MidiInputs> (this)]
        {
            if (ref != nullptr)
            {
                ref->refreshDevices();

                if (ref->onDevicesChanged != nullptr)
                    ref->onDevicesChanged();
            }
        });
    }
}
