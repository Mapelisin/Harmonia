#include "sandbox/Channel.h"
#include "sandbox/Protocol.h"

namespace harmonia::sandbox
{
    namespace
    {
        constexpr juce::uint32 magic = 0x48726d50;   // "HrmP"
    }

    Channel::Channel (bool callbacksOnMessageThread)
        : juce::InterprocessConnection (callbacksOnMessageThread, magic)
    {
    }

    Channel::~Channel()
    {
        close();
    }

    bool Channel::listen (const juce::String& pipeName)
    {
        return createPipe (pipeName, -1, true);
    }

    bool Channel::connect (const juce::String& pipeName)
    {
        lost = false;
        return connectToPipe (pipeName, -1);
    }

    void Channel::close()
    {
        closing = true;
        disconnect (-1, Notify::no);

        const juce::ScopedLock sl (pendingLock);

        for (auto* request : pending)
            request->answered.signal();
    }

    void Channel::setHandlers (std::function<void (const juce::ValueTree&)> messageHandler, std::function<void()> lostHandler)
    {
        const juce::ScopedLock sl (handlerLock);
        onMessage = std::move (messageHandler);
        onLost = std::move (lostHandler);
    }

    bool Channel::isOpen() const
    {
        return isConnected() && ! lost;
    }

    bool Channel::write (const juce::ValueTree& message)
    {
        const juce::ScopedLock sl (sendLock);
        return sendMessage (protocol::toBytes (message));
    }

    juce::ValueTree Channel::call (juce::ValueTree request, int timeoutMs)
    {
        if (lost || closing)
            return {};

        Pending waiting;
        waiting.id = nextId++;
        request.setProperty (protocol::property::id, waiting.id, nullptr);

        {
            const juce::ScopedLock sl (pendingLock);
            pending.push_back (&waiting);
        }

        if (write (request))
            waiting.answered.wait (timeoutMs);

        const juce::ScopedLock sl (pendingLock);
        pending.erase (std::find (pending.begin(), pending.end(), &waiting));
        return waiting.reply;
    }

    void Channel::send (const juce::ValueTree& message)
    {
        if (! lost && ! closing)
            write (message);
    }

    void Channel::reply (const juce::ValueTree& request, juce::ValueTree answer)
    {
        answer = answer.isValid() ? answer : juce::ValueTree (protocol::message::reply);

        if (answer.getType() != protocol::message::reply)
        {
            juce::ValueTree wrapped (protocol::message::reply);
            wrapped.copyPropertiesAndChildrenFrom (answer, nullptr);
            answer = wrapped;
        }

        answer.setProperty (protocol::property::id, request[protocol::property::id], nullptr);
        send (answer);
    }

    void Channel::connectionLost()
    {
        lost = true;

        {
            const juce::ScopedLock sl (pendingLock);

            for (auto* request : pending)
                request->answered.signal();
        }

        std::function<void()> handler;

        {
            const juce::ScopedLock sl (handlerLock);
            handler = onLost;
        }

        if (! closing && handler != nullptr)
            handler();
    }

    void Channel::messageReceived (const juce::MemoryBlock& bytes)
    {
        auto message = protocol::fromBytes (bytes);

        if (! message.isValid())
            return;

        if (message.getType() == protocol::message::reply)
        {
            const int id = message[protocol::property::id];
            const juce::ScopedLock sl (pendingLock);

            for (auto* request : pending)
            {
                if (request->id == id)
                {
                    request->reply = message;
                    request->answered.signal();
                }
            }

            return;
        }

        std::function<void (const juce::ValueTree&)> handler;

        {
            const juce::ScopedLock sl (handlerLock);
            handler = onMessage;
        }

        if (handler != nullptr)
            handler (message);
    }
}
