#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

/** What the plugin sandbox needs from the operating system, behind small classes: memory and
    signals two processes share, and the plugin's process itself. Windows only for now. */
namespace harmonia::sandbox::platform
{
    /** Memory Harmonia creates and the plugin's process opens by name. Zeroed when created. */
    class SharedMemory
    {
    public:
        static std::unique_ptr<SharedMemory> create (const juce::String& name, size_t size);
        static std::unique_ptr<SharedMemory> open (const juce::String& name, size_t size);
        ~SharedMemory();

        void* getData() const noexcept    { return data; }

    private:
        SharedMemory (void* handle, void* data);

        void* handle;
        void* data;

        JUCE_DECLARE_NON_COPYABLE (SharedMemory)
    };

    /** A signal one process raises and another waits for; each raise wakes one wait. */
    class Signal
    {
    public:
        static std::unique_ptr<Signal> create (const juce::String& name);
        static std::unique_ptr<Signal> open (const juce::String& name);
        ~Signal();

        void raise() noexcept;

        /** True once raised, false if `timeoutMs` passed first (-1: no limit). */
        bool wait (int timeoutMs) noexcept;

        /** Forgets a raise nobody has waited for yet. */
        void clear() noexcept;

    private:
        explicit Signal (void* handle);

        void* handle;

        JUCE_DECLARE_NON_COPYABLE (Signal)
    };

    /** A program Harmonia started. When Harmonia ends (even by crashing) the system ends it too. */
    class Process
    {
    public:
        static std::unique_ptr<Process> start (const juce::File& executable, const juce::StringArray& arguments);
        ~Process();

        bool isRunning() const noexcept;

        /** Ends it at once (any thread). */
        void kill() noexcept;

        /** True if it ended within the time. */
        bool waitForExit (int timeoutMs) noexcept;

        /** Its windows may come to the front (the system allows it only to the program in front). */
        void allowToComeToFront() noexcept;

    private:
        Process (void* handle, uint32_t id);

        void* handle;
        uint32_t id;

        JUCE_DECLARE_NON_COPYABLE (Process)
    };

    uint32_t getCurrentProcessId() noexcept;

    /** Calls `onEnded` (on a thread of its own) when process `id` ends; until destroyed. */
    class ProcessWatcher
    {
    public:
        ProcessWatcher (uint32_t id, std::function<void()> onEnded);
        ~ProcessWatcher();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE (ProcessWatcher)
    };

    /** In the plugin's process: a crash ends it quietly, with no system error window. */
    void prepareHostProcess() noexcept;

    /** Waits on the audio thread end within a millisecond of their deadline while it exists. */
    class FineTimerResolution
    {
    public:
        FineTimerResolution() noexcept;
        ~FineTimerResolution();

        JUCE_DECLARE_NON_COPYABLE (FineTimerResolution)
    };
}
