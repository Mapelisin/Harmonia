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

            /** Where recordings of projects that haven't been saved yet go (a saved project
                keeps them in a folder next to it). Default: Documents/Harmonia/Recordings. */
            juce::File recordingsFolder;
        };

        explicit Session (Options);
        ~Session();

        te::Engine& getEngine() noexcept           { return *engine; }
        bool devicesEnabled() const noexcept       { return openDevices; }

        /** The file a new recording on this track goes to: "<project> Audio" next to a saved
            project, else the recordings folder; named after the track and the time. */
        static juce::File getFileForNewRecording (const te::Track&, const juce::String& extension,
                                                  const juce::File& recordingsFolder);

    private:
        bool openDevices;
        std::unique_ptr<te::Engine> engine;

        JUCE_DECLARE_NON_COPYABLE (Session)
    };
}
