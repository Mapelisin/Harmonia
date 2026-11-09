#include "sandbox/Platform.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>

#include <crtdbg.h>
#include <cstdlib>
#include <string>
#include <thread>

#pragma comment (lib, "winmm.lib")

namespace harmonia::sandbox::platform
{
    namespace
    {
        /** Objects of this session only (other users' programs can't see them). */
        juce::String objectName (const juce::String& name)
        {
            return "Local\\" + name;
        }

        /** Every plugin process is in it; when Harmonia ends, its handle closes and the system
            ends them all, so none is left running after a crash of Harmonia itself. */
        HANDLE getJob()
        {
            static const HANDLE job = []
            {
                auto handle = CreateJobObjectW (nullptr, nullptr);

                if (handle != nullptr)
                {
                    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
                    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                    SetInformationJobObject (handle, JobObjectExtendedLimitInformation, &limits, sizeof (limits));
                }

                return handle;
            }();

            return job;
        }

        juce::String quoted (const juce::String& argument)
        {
            return argument.containsAnyOf (" \t") ? argument.quoted() : argument;
        }

        LONG WINAPI endQuietly (EXCEPTION_POINTERS*)
        {
            TerminateProcess (GetCurrentProcess(), 3);
            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    //==============================================================================
    SharedMemory::SharedMemory (void* h, void* d) : handle (h), data (d) {}

    SharedMemory::~SharedMemory()
    {
        UnmapViewOfFile (data);
        CloseHandle (handle);
    }

    std::unique_ptr<SharedMemory> SharedMemory::create (const juce::String& name, size_t size)
    {
        const auto size64 = (uint64_t) size;
        auto handle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                          (DWORD) (size64 >> 32), (DWORD) (size64 & 0xffffffffu),
                                          objectName (name).toWideCharPointer());

        if (handle == nullptr)
            return {};

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle (handle);
            return {};
        }

        if (auto* data = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, size))
            return std::unique_ptr<SharedMemory> (new SharedMemory (handle, data));

        CloseHandle (handle);
        return {};
    }

    std::unique_ptr<SharedMemory> SharedMemory::open (const juce::String& name, size_t size)
    {
        auto handle = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, objectName (name).toWideCharPointer());

        if (handle == nullptr)
            return {};

        if (auto* data = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, size))
            return std::unique_ptr<SharedMemory> (new SharedMemory (handle, data));

        CloseHandle (handle);
        return {};
    }

    //==============================================================================
    Signal::Signal (void* h) : handle (h) {}
    Signal::~Signal()    { CloseHandle (handle); }

    std::unique_ptr<Signal> Signal::create (const juce::String& name)
    {
        auto handle = CreateEventW (nullptr, FALSE, FALSE, objectName (name).toWideCharPointer());

        if (handle == nullptr)
            return {};

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle (handle);
            return {};
        }

        return std::unique_ptr<Signal> (new Signal (handle));
    }

    std::unique_ptr<Signal> Signal::open (const juce::String& name)
    {
        if (auto handle = OpenEventW (EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, objectName (name).toWideCharPointer()))
            return std::unique_ptr<Signal> (new Signal (handle));

        return {};
    }

    void Signal::raise() noexcept    { SetEvent (handle); }
    void Signal::clear() noexcept    { ResetEvent (handle); }

    bool Signal::wait (int timeoutMs) noexcept
    {
        return WaitForSingleObject (handle, timeoutMs < 0 ? INFINITE : (DWORD) timeoutMs) == WAIT_OBJECT_0;
    }

    //==============================================================================
    Process::Process (void* h, uint32_t processId) : handle (h), id (processId) {}
    Process::~Process()    { CloseHandle (handle); }

    std::unique_ptr<Process> Process::start (const juce::File& executable, const juce::StringArray& arguments)
    {
        auto commandLine = quoted (executable.getFullPathName());

        for (const auto& argument : arguments)
            commandLine << " " << quoted (argument);

        STARTUPINFOW startup {};
        startup.cb = sizeof (startup);
        PROCESS_INFORMATION info {};

        // A writable copy: CreateProcessW may change the command line it's given.
        std::wstring buffer (commandLine.toWideCharPointer());

        // Suspended until it is in the job, so it can't start anything outside it first.
        if (! CreateProcessW (executable.getFullPathName().toWideCharPointer(), buffer.data(), nullptr, nullptr, FALSE,
                              CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
            return {};

        if (auto job = getJob())
            AssignProcessToJobObject (job, info.hProcess);

        ResumeThread (info.hThread);
        CloseHandle (info.hThread);

        return std::unique_ptr<Process> (new Process (info.hProcess, (uint32_t) info.dwProcessId));
    }

    bool Process::isRunning() const noexcept
    {
        return WaitForSingleObject (handle, 0) == WAIT_TIMEOUT;
    }

    void Process::kill() noexcept
    {
        TerminateProcess (handle, 2);
    }

    bool Process::waitForExit (int timeoutMs) noexcept
    {
        return WaitForSingleObject (handle, timeoutMs < 0 ? INFINITE : (DWORD) timeoutMs) == WAIT_OBJECT_0;
    }

    void Process::allowToComeToFront() noexcept
    {
        AllowSetForegroundWindow ((DWORD) id);
    }

    uint32_t getCurrentProcessId() noexcept
    {
        return (uint32_t) GetCurrentProcessId();
    }

    //==============================================================================
    struct ProcessWatcher::Impl
    {
        HANDLE process = nullptr;
        HANDLE stop = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        std::thread thread;

        ~Impl()
        {
            SetEvent (stop);

            if (thread.joinable())
                thread.join();

            if (process != nullptr)
                CloseHandle (process);

            CloseHandle (stop);
        }
    };

    ProcessWatcher::ProcessWatcher (uint32_t id, std::function<void()> onEnded)
        : impl (std::make_unique<Impl>())
    {
        impl->process = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) id);

        // A process that can't be opened has already ended.
        if (impl->process == nullptr)
        {
            impl->thread = std::thread (std::move (onEnded));
            return;
        }

        impl->thread = std::thread ([process = impl->process, stop = impl->stop, onEnded = std::move (onEnded)]
        {
            HANDLE handles[] = { process, stop };

            if (WaitForMultipleObjects (2, handles, FALSE, INFINITE) == WAIT_OBJECT_0)
                onEnded();
        });
    }

    ProcessWatcher::~ProcessWatcher() = default;

    //==============================================================================
    void prepareHostProcess() noexcept
    {
        SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        SetUnhandledExceptionFilter (endQuietly);
        _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode (_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        _CrtSetReportMode (_CRT_ERROR, _CRTDBG_MODE_DEBUG);
    }

    FineTimerResolution::FineTimerResolution() noexcept    { timeBeginPeriod (1); }
    FineTimerResolution::~FineTimerResolution()            { timeEndPeriod (1); }
}
