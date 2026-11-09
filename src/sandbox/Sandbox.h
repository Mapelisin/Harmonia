#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace harmonia::sandbox
{
    class RemotePlugin;

    /** Runs each plugin in a process of its own: a plugin that crashes or hangs goes silent and
        Harmonia plays on. The program started for each plugin is `executable`, which hands its
        command line to Host::fromCommandLine (Harmonia.exe does, and so does the test program). */
    class Sandbox
    {
    public:
        struct Options
        {
            juce::File executable;

            /** A plugin busy with one block of sound for longer than this is taken as hung and
                stopped (ten times as long while rendering to a file). */
            int hangLimitMs = 5000;
        };

        explicit Sandbox (Options);
        ~Sandbox();

        /** Starts a process and loads the plugin in it (message thread; it may take a while, as
            loading a plugin does): nullptr, and why in `error`, if it can't. */
        std::unique_ptr<RemotePlugin> createInstance (const juce::PluginDescription&, double sampleRate,
                                                      int blockSize, juce::String& error);

        struct Listener
        {
            virtual ~Listener() = default;

            /** A plugin's process crashed or hung: the plugin is silent now (message thread). */
            virtual void pluginCrashed (RemotePlugin&) = 0;

            /** RemotePlugin::reload() started it again. */
            virtual void pluginReloaded (RemotePlugin&) {}
        };

        void addListener (Listener*);
        void removeListener (Listener*);

        /** What the sandbox and its plugins share (a plugin may outlive the sandbox: Tracktion
            deletes some later). */
        class Shared;

    private:
        std::shared_ptr<Shared> shared;

        JUCE_DECLARE_NON_COPYABLE (Sandbox)
    };
}
