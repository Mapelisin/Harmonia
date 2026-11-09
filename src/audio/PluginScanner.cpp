#include "audio/PluginScanner.h"

namespace harmonia::audio
{
    namespace
    {
        const juce::String foldersKey ("pluginFolders");

        // Two scans at once would write the same plugin list from two threads.
        std::atomic<PluginScanner*> scanning { nullptr };

        void finished (PluginScanner* scanner)
        {
            auto expected = scanner;
            scanning.compare_exchange_strong (expected, nullptr);
        }

        void saveFolders (core::Session& session, const juce::StringArray& folders)
        {
            auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
            settings.setValue (foldersKey, folders.joinIntoString ("\n"));
            settings.saveIfNeeded();
        }
    }

    juce::StringArray PluginScanner::getOwnFolders (core::Session& session)
    {
        auto folders = juce::StringArray::fromLines (session.getEngine().getPropertyStorage().getPropertiesFile().getValue (foldersKey));
        folders.removeEmptyStrings();
        return folders;
    }

    void PluginScanner::addFolder (core::Session& session, const juce::File& folder)
    {
        if (! folder.isDirectory())
            return;

        auto folders = getOwnFolders (session);

        for (const auto& path : folders)
            if (juce::File (path) == folder)
                return;

        folders.add (folder.getFullPathName());
        saveFolders (session, folders);
    }

    void PluginScanner::removeFolder (core::Session& session, const juce::String& path)
    {
        auto folders = getOwnFolders (session);
        folders.removeString (path, true);
        saveFolders (session, folders);
    }

    juce::FileSearchPath PluginScanner::getSearchPath (core::Session& session, juce::AudioPluginFormat& format)
    {
        auto path = format.getDefaultLocationsToSearch();

        // Plugins in folders of one's own are VST3 (other formats are found where the system keeps them).
        if (format.getName() == "VST3")
            for (const auto& folder : getOwnFolders (session))
                if (const juce::File directory (folder); directory.isDirectory())
                    path.addIfNotAlreadyThere (directory);

        return path;
    }

    PluginScanner::PluginScanner (core::Session& s)
        : juce::Thread ("Harmonia plugin scan"),
          session (s)
    {
        self = this;
    }

    PluginScanner::~PluginScanner()
    {
        cancelScan();
    }

    juce::Array<juce::AudioPluginFormat*> PluginScanner::getFormatsToScan() const
    {
        juce::Array<juce::AudioPluginFormat*> formats;

        for (auto* format : session.getEngine().getPluginManager().pluginFormatManager.getFormats())
            if (format->getName() == "VST3" || format->getName() == "AudioUnit")
                formats.add (format);

        return formats;
    }

    bool PluginScanner::startScan (Callbacks newCallbacks)
    {
        PluginScanner* none = nullptr;

        if (isThreadRunning() || ! scanning.compare_exchange_strong (none, this))
            return false;

        callbacks = std::move (newCallbacks);

        if (startThread())
            return true;

        finished (this);
        return false;
    }

    bool PluginScanner::isAnyScanRunning()
    {
        return scanning.load() != nullptr;
    }

    void PluginScanner::cancelScan()
    {
        if (! isThreadRunning())
            return;

        signalThreadShouldExit();

        if (auto& abort = session.getEngine().getPluginManager().abortCurrentPluginScan)
            abort();

        stopThread (10000);
        finished (this);
    }

    bool PluginScanner::isScanning() const
    {
        return isThreadRunning();
    }

    void PluginScanner::run()
    {
        auto& list = session.getEngine().getPluginManager().knownPluginList;

        auto post = [ref = self] (std::function<void (PluginScanner&)> f)
        {
            juce::MessageManager::callAsync ([ref, f = std::move (f)]
            {
                if (auto* scanner = ref.get())
                    f (*scanner);
            });
        };

        for (auto* format : getFormatsToScan())
        {
            // No dead-man's-pedal file: crashes are contained by the child process.
            juce::PluginDirectoryScanner scanner (list, *format, getSearchPath (session, *format),
                                                  true, juce::File(), false);

            while (! threadShouldExit())
            {
                const auto next = scanner.getNextPluginFileThatWillBeScanned();

                post ([next] (PluginScanner& s)
                {
                    if (s.callbacks.onProgress)
                        s.callbacks.onProgress (next);
                });

                juce::String name;

                if (! scanner.scanNextFile (true, name))
                    break;
            }
        }

        const auto numKnown = list.getNumTypes();
        finished (this);

        post ([numKnown] (PluginScanner& s)
        {
            if (s.callbacks.onFinished)
                s.callbacks.onFinished (numKnown);
        });
    }

    juce::Array<juce::PluginDescription> PluginScanner::scanFile (const juce::String& path)
    {
        juce::Array<juce::PluginDescription> result;
        auto& list = session.getEngine().getPluginManager().knownPluginList;

        for (auto* format : getFormatsToScan())
        {
            if (! format->fileMightContainThisPluginType (path))
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            list.scanAndAddFile (path, false, found, *format);

            for (auto* description : found)
                result.add (*description);
        }

        return result;
    }
}
