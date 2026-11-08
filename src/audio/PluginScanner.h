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

        /** Scans the standard plugin folders. Does nothing if a scan is already running. */
        void startScan (Callbacks);
        void cancelScan();
        bool isScanning() const;

        /** Scans one plugin file on the calling thread and returns what it contains. */
        juce::Array<juce::PluginDescription> scanFile (const juce::String& path);

    private:
        core::Session& session;
        Callbacks callbacks;
        juce::WeakReference<PluginScanner> self;

        void run() override;
        juce::Array<juce::AudioPluginFormat*> getFormatsToScan() const;

        JUCE_DECLARE_WEAK_REFERENCEABLE (PluginScanner)
        JUCE_DECLARE_NON_COPYABLE (PluginScanner)
    };
}
