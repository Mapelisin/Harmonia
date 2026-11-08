#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** The MIDI keyboards plugged into the computer.

        Every keyboard plays the instrument of one track (the track being edited). The engine
        does that itself, with sample-accurate timing and no detour through the message thread,
        so playing feels as tight as the audio buffer allows.

        The notes are also handed to `onMessage` (on the message thread, with the moment each
        message arrived) so they can be recorded. Keyboards plugged in later are picked up. */
    class MidiInputs final : private juce::ChangeListener,
                             private te::PhysicalMidiInputDevice::Listener,
                             private juce::AsyncUpdater
    {
    public:
        explicit MidiInputs (te::Edit&);
        ~MidiInputs() override;

        /** Routes every keyboard to this track's instrument (nullptr: to none). */
        void setTrack (te::AudioTrack*);

        /** Names of the keyboards in use. */
        juce::StringArray getDeviceNames() const;

        /** Notes, pedal and other messages from any keyboard, with the time they arrived
            (juce::Time::getMillisecondCounterHiRes()). Called on the message thread. */
        std::function<void (const juce::MidiMessage&, double wallMs)> onMessage;

        /** Called when keyboards are plugged in or out. */
        std::function<void()> onDevicesChanged;

    private:
        struct Received
        {
            juce::MidiMessage message;
            double wallMs = 0.0;
        };

        te::Edit& edit;
        te::DeviceManager& devices;
        te::Selectable::WeakRef track;
        std::vector<std::shared_ptr<te::PhysicalMidiInputDevice>> listening;

        juce::CriticalSection queueLock;
        std::vector<Received> queue, delivering;

        void refreshDevices();
        void routeToTrack();

        void handleIncomingMidiMessage (const juce::MidiMessage&) override;   // MIDI thread
        void handleAsyncUpdate() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        JUCE_DECLARE_WEAK_REFERENCEABLE (MidiInputs)
        JUCE_DECLARE_NON_COPYABLE (MidiInputs)
    };
}
