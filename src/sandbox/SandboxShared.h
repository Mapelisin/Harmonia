#pragma once

#include "sandbox/Platform.h"
#include "sandbox/Sandbox.h"

#include <vector>

namespace harmonia::sandbox
{
    /** The program to start, the listeners, and a thread that stops plugins that hang and
        processes that don't end when asked to. */
    class Sandbox::Shared final : private juce::Thread
    {
    public:
        explicit Shared (Options);
        ~Shared() override;

        const Options options;

        /** Message thread only. */
        juce::ListenerList<Sandbox::Listener> listeners;

        void add (RemotePlugin*);
        void remove (RemotePlugin*);

        /** A process asked to end: stopped if it's still running after a few seconds. */
        void retire (std::unique_ptr<platform::Process>);

        /** A name no other plugin process, pipe or shared memory has. */
        juce::String makeName();

    private:
        struct Retiring
        {
            std::unique_ptr<platform::Process> process;
            double deadlineMs;
        };

        platform::FineTimerResolution timerResolution;
        juce::CriticalSection lock;
        std::vector<RemotePlugin*> plugins;
        std::vector<Retiring> retiring;
        std::atomic<int> nextName { 1 };

        void run() override;

        JUCE_DECLARE_NON_COPYABLE (Shared)
    };
}
