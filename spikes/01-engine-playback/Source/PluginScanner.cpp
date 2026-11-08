#include "PluginScanner.h"

namespace harmonia::spike
{
    PluginScanner::PluginScanner (te::Engine& e)
        : engine (e)
    {
        load();
    }

    juce::AudioPluginFormat* PluginScanner::findVst3Format() const
    {
        for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
            if (format->getName() == "VST3")
                return format;

        return nullptr;
    }

    juce::File PluginScanner::getListFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Harmonia")
                   .getChildFile ("Spike01")
                   .getChildFile ("plugins.xml");
    }

    void PluginScanner::load()
    {
        if (auto xml = juce::parseXML (getListFile()))
            engine.getPluginManager().knownPluginList.recreateFromXml (*xml);
    }

    void PluginScanner::save()
    {
        if (auto xml = engine.getPluginManager().knownPluginList.createXml())
        {
            getListFile().getParentDirectory().createDirectory();
            xml->writeTo (getListFile());
        }
    }

    int PluginScanner::scanDefaultVst3Folders (const std::function<void (const juce::String&)>& onPluginStarted)
    {
        auto& list = engine.getPluginManager().knownPluginList;
        auto* format = findVst3Format();

        if (format == nullptr)
            return list.getNumTypes();

        const auto deadMansPedal = getListFile().getSiblingFile ("scan-in-progress.txt");

        juce::PluginDirectoryScanner scanner (list, *format, format->getDefaultLocationsToSearch(),
                                              true, deadMansPedal, false);

        for (;;)
        {
            if (onPluginStarted)
                onPluginStarted (scanner.getNextPluginFileThatWillBeScanned());

            juce::String name;

            if (! scanner.scanNextFile (true, name))
                break;
        }

        save();
        return list.getNumTypes();
    }

    juce::Array<juce::PluginDescription> PluginScanner::scanFile (const juce::String& path)
    {
        juce::Array<juce::PluginDescription> result;
        auto* format = findVst3Format();

        if (format == nullptr)
            return result;

        juce::OwnedArray<juce::PluginDescription> found;
        engine.getPluginManager().knownPluginList.scanAndAddFile (path, false, found, *format);

        for (auto* d : found)
            result.add (*d);

        save();
        return result;
    }

    juce::Array<juce::PluginDescription> PluginScanner::getInstruments() const
    {
        juce::Array<juce::PluginDescription> result;

        for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
            if (d.isInstrument)
                result.add (d);

        return result;
    }
}
