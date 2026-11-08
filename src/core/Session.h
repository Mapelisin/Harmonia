#pragma once

#include "core/Tracktion.h"

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
        };

        explicit Session (Options);
        ~Session();

        te::Engine& getEngine() noexcept           { return *engine; }
        bool devicesEnabled() const noexcept       { return openDevices; }

    private:
        bool openDevices;
        std::unique_ptr<te::Engine> engine;

        JUCE_DECLARE_NON_COPYABLE (Session)
    };
}
