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

        /** Notes, pedals, wheels, knobs and key pressure from any keyboard, with the time they
            arrived (juce::Time::getMillisecondCounterHiRes()). Called on the message thread. */
        std::function<void (const juce::MidiMessage&, double wallMs)> onMessage;

        /** True for a message that goes to onMessage: a note, a controller (pedals, wheels,
            knobs), the pitch bend, the pressure on the keys. Not clocks, sysex and the like. */
        static bool isForwarded (const juce::MidiMessage&) noexcept;

        /** A message as a keyboard sends it (any thread): one that isForwarded reaches onMessage
            on the message thread. Every keyboard's messages come through here. */
        void post (const juce::MidiMessage&);

        /** Called when keyboards are plugged in or out. */
        std::function<void()> onDevicesChanged;

        /** A MIDI keyboard plugged into the computer. */
        struct Keyboard
        {
            juce::String id, name;
            bool on = true;
        };

        /** Every keyboard plugged in, those switched off included. */
        static std::vector<Keyboard> getKeyboards (te::Engine&);

        /** Switches a keyboard on or off (Tracktion keeps it in the settings): one that's off
            plays nothing and isn't recorded, as if it were unplugged. */
        static void setKeyboardOn (te::Engine&, const juce::String& id, bool on);

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
