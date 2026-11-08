#pragma once

#include "Common.h"

namespace harmonia::spike
{
    /** Finds VST3 plugins and keeps the engine's KnownPluginList on disk between runs.
        Scanning happens in-process on the calling thread (good enough for a spike;
        the real app must scan out-of-process so a broken plugin can't crash it). */
    class PluginScanner
    {
    public:
        explicit PluginScanner (te::Engine&);

        /** Scans the default VST3 folders. Returns the number of known plugins afterwards. */
        int scanDefaultVst3Folders (const std::function<void (const juce::String&)>& onPluginStarted);

        /** Scans a single plugin file and returns what it contains. */
        juce::Array<juce::PluginDescription> scanFile (const juce::String& path);

        juce::Array<juce::PluginDescription> getInstruments() const;

    private:
        te::Engine& engine;

        juce::AudioPluginFormat* findVst3Format() const;
        juce::File getListFile() const;
        void load();
        void save();
    };
}
