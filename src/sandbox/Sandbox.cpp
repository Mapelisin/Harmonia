#include "sandbox/Sandbox.h"
#include "sandbox/RemotePlugin.h"
#include "sandbox/SandboxShared.h"

#include <algorithm>

namespace harmonia::sandbox
{
    namespace
    {
        double now()    { return juce::Time::getMillisecondCounterHiRes(); }
    }

    Sandbox::Shared::Shared (Options o)
        : juce::Thread ("Plugin watchdog"), options (std::move (o))
    {
        startThread();
    }

    Sandbox::Shared::~Shared()
    {
        stopThread (2000);

        // Processes still ending get a moment more, then they are stopped.
        const auto deadline = now() + 2000;

        for (auto& process : retiring)
            if (const auto left = deadline - now(); left <= 0 || ! process.process->waitForExit ((int) left))
                process.process->kill();
    }

    void Sandbox::Shared::add (RemotePlugin* plugin)
    {
        const juce::ScopedLock sl (lock);
        plugins.push_back (plugin);
    }

    void Sandbox::Shared::remove (RemotePlugin* plugin)
    {
        const juce::ScopedLock sl (lock);
        plugins.erase (std::remove (plugins.begin(), plugins.end(), plugin), plugins.end());
    }

    void Sandbox::Shared::retire (std::unique_ptr<platform::Process> process)
    {
        if (process == nullptr)
            return;

        const juce::ScopedLock sl (lock);
        retiring.push_back ({ std::move (process), now() + 5000 });
    }

    juce::String Sandbox::Shared::makeName()
    {
        return "harmonia-" + juce::String (platform::getCurrentProcessId()) + "-" + juce::String (nextName++)
                 + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt());
    }

    void Sandbox::Shared::run()
    {
        while (! threadShouldExit())
        {
            wait (100);

            const auto time = now();
            const juce::ScopedLock sl (lock);

            for (auto* plugin : plugins)
                plugin->checkForHang (time);

            retiring.erase (std::remove_if (retiring.begin(), retiring.end(), [time] (Retiring& r)
            {
                if (! r.process->isRunning())
                    return true;

                if (time < r.deadlineMs)
                    return false;

                r.process->kill();
                return true;
            }), retiring.end());
        }
    }

    //==============================================================================
    Sandbox::Sandbox (Options options)
        : shared (std::make_shared<Shared> (std::move (options)))
    {
    }

    Sandbox::~Sandbox() = default;

    std::unique_ptr<RemotePlugin> Sandbox::createInstance (const juce::PluginDescription& description, double sampleRate,
                                                           int blockSize, juce::String& error)
    {
        return RemotePlugin::load (shared, description, sampleRate, blockSize, error);
    }

    void Sandbox::addListener (Listener* listener)       { shared->listeners.add (listener); }
    void Sandbox::removeListener (Listener* listener)    { shared->listeners.remove (listener); }
}
