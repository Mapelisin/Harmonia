#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Finds installed plugins (VST3; AU on macOS) and adds them to the engine's plugin list,
        which Tracktion saves with the settings.

        With Session::Options::scanPluginsOutOfProcess every plugin file is examined in a child
        process: a plugin that crashes while being scanned is skipped instead of taking
        Harmonia down. The folder scan runs on a background thread; callbacks arrive on the
        message thread. */
    class PluginScanner : private juce::Thread
    {
    public:
        explicit PluginScanner (core::Session&);
        ~PluginScanner() override;

        struct Callbacks
        {
            std::function<void (const juce::String& pluginBeingScanned)> onProgress;
            std::function<void (int numPluginsKnown)> onFinished;
        };

        /** Scans the standard plugin folders and the user's own (see addFolder). One scan at a
            time in the whole program: false (and nothing happens) if one is running already. */
        bool startScan (Callbacks);
        void cancelScan();
        bool isScanning() const;

        /** Whether any scanner is scanning (the instrument panel's, or the settings window's). */
        static bool isAnyScanRunning();

        /** Scans one plugin file on the calling thread and returns what it contains. */
        juce::Array<juce::PluginDescription> scanFile (const juce::String& path);

        /** Folders of plugins outside the standard ones (E:\VST...) a scan also looks in, every
            folder inside them included: chosen by the user, kept in the settings. */
        static juce::StringArray getOwnFolders (core::Session&);
        static void addFolder (core::Session&, const juce::File&);
        static void removeFolder (core::Session&, const juce::String& path);

        /** Where a scan looks for this format's plugins: its standard folders, then the user's. */
        static juce::FileSearchPath getSearchPath (core::Session&, juce::AudioPluginFormat&);

        /** The plugin formats a scan looks for (VST3; AU on macOS). */
        juce::Array<juce::AudioPluginFormat*> getFormatsToScan() const;

    private:
        core::Session& session;
        Callbacks callbacks;
        juce::WeakReference<PluginScanner> self;

        void run() override;

        JUCE_DECLARE_WEAK_REFERENCEABLE (PluginScanner)
        JUCE_DECLARE_NON_COPYABLE (PluginScanner)
    };
}
