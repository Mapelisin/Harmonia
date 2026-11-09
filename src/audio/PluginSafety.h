#pragma once

#include "core/Session.h"

namespace harmonia::sandbox { class RemotePlugin; }

/** Plugins that run in processes of their own (core::Session's sandbox): which crashed, bringing
    them back, their own windows. A crashed plugin is silent (an effect lets the sound through) and
    the rest of the song plays on. */
namespace harmonia::audio::plugin_safety
{
    enum class Status
    {
        notSandboxed,   ///< built in, or loaded into Harmonia's own process
        running,
        crashed         ///< crashed or hung: silent until reloaded
    };

    Status getStatus (te::Plugin&);

    /** It was stopped for hanging (not answering), not for crashing. */
    bool wasHung (te::Plugin&);

    /** Starts a crashed plugin again, in the state it had (message thread): an error, or empty. */
    juce::String reload (te::Plugin&);

    /** For the user: what happened to it and what to do ("Kontakt on Piano crashed..."). */
    juce::String describeCrash (te::Plugin&);

    /** The plugin's own window comes to the front, if it is open (it is another program's). */
    void bringWindowToFront (te::Plugin&);

    /** The stand-in for a plugin that runs in a process of its own, or nullptr. */
    sandbox::RemotePlugin* getRemote (te::Plugin&);

    /** Says which plugin of an open song crashed or came back (message thread). */
    class Watcher final
    {
    public:
        Watcher (core::Session&, std::function<void (te::Plugin&)> onChanged);
        ~Watcher();

    private:
        class Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE (Watcher)
    };
}
