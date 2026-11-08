#include "audio/PluginScanner.h"

namespace harmonia::audio
{
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

    void PluginScanner::startScan (Callbacks newCallbacks)
    {
        if (isThreadRunning())
            return;

        callbacks = std::move (newCallbacks);
        startThread();
    }

    void PluginScanner::cancelScan()
    {
        if (! isThreadRunning())
            return;

        signalThreadShouldExit();

        if (auto& abort = session.getEngine().getPluginManager().abortCurrentPluginScan)
            abort();

        stopThread (10000);
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
            juce::PluginDirectoryScanner scanner (list, *format, format->getDefaultLocationsToSearch(),
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
