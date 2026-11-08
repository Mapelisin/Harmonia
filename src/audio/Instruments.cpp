#include "audio/Instruments.h"

namespace harmonia::audio::instruments
{
    namespace
    {
        juce::String replaceWith (te::AudioTrack& track, te::Plugin::Ptr plugin)
        {
            if (plugin == nullptr)
                return "The instrument could not be created";

            if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
                if (external->getAudioPluginInstance() == nullptr)
                    return "The plugin failed to load: " + external->getLoadError();

            auto* previous = get (track);

            // Insert the new one before removing the old one: playback keeps running on the
            // current graph until the engine swaps in the rebuilt one.
            track.pluginList.insertPlugin (plugin, 0, nullptr);

            if (previous != nullptr)
            {
                previous->hideWindowForShutdown();
                previous->deleteFromParent();
            }

            return {};
        }
    }

    te::Plugin* get (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList.getPlugins())
            if (plugin->isSynth())
                return plugin;

        return nullptr;
    }

    juce::String getName (te::AudioTrack& track)
    {
        if (auto* plugin = get (track))
            return plugin->getName();

        return {};
    }

    juce::String useBuiltInSynth (te::AudioTrack& track)
    {
        return replaceWith (track, track.edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}));
    }

    juce::String useExternal (te::AudioTrack& track, const juce::PluginDescription& description)
    {
        if (! description.isInstrument)
            return description.name + " is not an instrument";

        return replaceWith (track, track.edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName,
                                                                                 description));
    }

    juce::Array<juce::PluginDescription> getAvailable (core::Session& session)
    {
        juce::Array<juce::PluginDescription> result;

        for (const auto& description : session.getEngine().getPluginManager().knownPluginList.getTypes())
            if (description.isInstrument)
                result.add (description);

        std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b)
        {
            return a.name.compareNatural (b.name) < 0;
        });

        return result;
    }

    void showEditor (te::AudioTrack& track)
    {
        if (auto* plugin = get (track))
            plugin->showWindowExplicitly();
    }
}
