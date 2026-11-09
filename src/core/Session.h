#pragma once

#include "core/Tracktion.h"
#include "sandbox/Sandbox.h"

namespace harmonia::core
{
    /** Owns the Tracktion Engine instance shared by the model (core) and audio layers.
        One Session lives for the whole lifetime of the application. */
    class Session
    {
    public:
        struct Options
        {
            /** False for tests and tools: no audio or MIDI devices are opened. */
            bool openDevices = true;

            /** How the engine opens plugin windows etc.; a default one is used if empty. */
            std::unique_ptr<te::UIBehaviour> uiBehaviour;

            /** Folder name for settings (devices, plugin list) under the user's app data.
                Tests use their own so they never touch the real app's settings. */
            juce::String settingsName = "Harmonia";

            /** Scan plugins in a child process (the app must hand the child's command line to
                te::PluginManager::startChildProcessPluginScan). */
            bool scanPluginsOutOfProcess = false;

            /** Where recordings of projects that haven't been saved yet go (a saved project
                keeps them in a folder next to it). Default: Documents/Harmonia/Recordings. */
            juce::File recordingsFolder;

            /** The program each plugin runs in, a process of its own per plugin (it must hand its
                command line to sandbox::Host::fromCommandLine): a plugin that crashes or hangs then
                only silences itself. Empty: plugins load into Harmonia's own process. */
            juce::File pluginHost;

            /** How long a plugin may take over one block of sound before it counts as hung. */
            int pluginHangLimitMs = 5000;
        };

        explicit Session (Options);
        ~Session();

        te::Engine& getEngine() noexcept           { return *engine; }
        bool devicesEnabled() const noexcept       { return openDevices; }

        /** Where plugins run in processes of their own; nullptr if they load into this one. */
        sandbox::Sandbox* getPluginSandbox() noexcept    { return pluginSandbox.get(); }

        /** The file a new recording on this track goes to: "<project> Audio" next to a saved
            project, else the recordings folder; named after the track and the time. */
        static juce::File getFileForNewRecording (const te::Track&, const juce::String& extension,
                                                  const juce::File& recordingsFolder);

    private:
        bool openDevices;

        // Outlives the engine: the engine's plugins are its processes.
        std::unique_ptr<sandbox::Sandbox> pluginSandbox;
        std::unique_ptr<te::Engine> engine;

        JUCE_DECLARE_NON_COPYABLE (Session)
    };
}
