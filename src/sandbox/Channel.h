#pragma once

#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <vector>

namespace harmonia::sandbox
{
    /** Messages (ValueTrees) between Harmonia and a plugin's process over a named pipe: requests
        that wait for their reply, and one-way notes. The plugin's process listens, Harmonia
        connects: so Harmonia learns at once when the plugin's process ends. */
    class Channel final : private juce::InterprocessConnection
    {
    public:
        /** The plugin's process handles messages on its message thread (plugins expect that);
            Harmonia handles them on the pipe's thread, so its message thread can wait for replies. */
        explicit Channel (bool callbacksOnMessageThread);
        ~Channel() override;

        /** The plugin's process: opens the pipe for Harmonia to connect to. */
        bool listen (const juce::String& pipeName);

        /** Harmonia: connects to the pipe a plugin's process opened (false if it isn't there yet). */
        bool connect (const juce::String& pipeName);

        /** Closes it without telling onLost. */
        void close();

        bool isOpen() const;

        /** Sends `request` and waits for the reply: an invalid tree if none came within
            `timeoutMs` or the pipe broke. Not on the thread that handles messages. */
        juce::ValueTree call (juce::ValueTree request, int timeoutMs);

        /** A one-way note (any thread). */
        void send (const juce::ValueTree& message);

        /** Answers a request received through onMessage. */
        void reply (const juce::ValueTree& request, juce::ValueTree reply);

        /** `onMessage` gets the other side's requests and notes (replies go to `call`); `onLost`
            is told when the other side ends (Harmonia's end only: the listening end can't tell).
            They may be replaced while messages come in. */
        void setHandlers (std::function<void (const juce::ValueTree&)> onMessage, std::function<void()> onLost);

    private:
        struct Pending
        {
            int id;
            juce::WaitableEvent answered;
            juce::ValueTree reply;
        };

        juce::CriticalSection pendingLock, sendLock, handlerLock;
        std::function<void (const juce::ValueTree&)> onMessage;
        std::function<void()> onLost;
        std::vector<Pending*> pending;
        std::atomic<int> nextId { 1 };
        std::atomic<bool> lost { false }, closing { false };

        void connectionMade() override {}
        void connectionLost() override;
        void messageReceived (const juce::MemoryBlock&) override;
        bool write (const juce::ValueTree&);

        JUCE_DECLARE_NON_COPYABLE (Channel)
    };
}
