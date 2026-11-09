#include "audio/Sidechain.h"

#include <set>

namespace harmonia::audio::sidechain
{
    namespace
    {
        /** True if the sound of `track` depends on `on`: it is `on`, or something it hears (a track
            playing into it, a send to its return, a track one of its effects listens to) depends on it. */
        bool dependsOn (const te::AudioTrack& track, const te::AudioTrack& on, std::set<const te::AudioTrack*>& seen)
        {
            if (&track == &on)
                return true;

            if (! seen.insert (&track).second)
                return false;

            const auto allTracks = te::getAudioTracks (track.edit);

            for (auto* other : allTracks)
                if (other->getOutput().getDestinationTrack() == &track && dependsOn (*other, on, seen))
                    return true;

            for (auto* plugin : track.pluginList)
            {
                if (auto* source = getSource (*plugin); source != nullptr && dependsOn (*source, on, seen))
                    return true;

                if (auto* returnPlugin = dynamic_cast<te::AuxReturnPlugin*> (plugin))
                    for (auto* other : allTracks)
                        for (auto* otherPlugin : other->pluginList)
                            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (otherPlugin);
                                send != nullptr && send->busNumber.get() == returnPlugin->busNumber.get() && dependsOn (*other, on, seen))
                                return true;
            }

            return false;
        }
    }

    bool canListen (te::Plugin& plugin)
    {
        return plugin.canSidechain();
    }

    juce::Array<te::AudioTrack*> getPossibleSources (const core::Project& project, te::Plugin& plugin)
    {
        juce::Array<te::AudioTrack*> sources;
        const auto* own = dynamic_cast<te::AudioTrack*> (plugin.getOwnerTrack());

        // The master's effects hear every track already: they don't get a sidechain.
        if (own == nullptr || ! canListen (plugin))
            return sources;

        for (auto* track : project.getTracks())
        {
            std::set<const te::AudioTrack*> seen;

            if (! dependsOn (*track, *own, seen))
                sources.add (track);
        }

        return sources;
    }

    te::AudioTrack* getSource (te::Plugin& plugin)
    {
        const auto id = plugin.getSidechainSourceID();
        return id.isValid() ? te::findAudioTrackForID (plugin.edit, id) : nullptr;
    }

    bool setSource (core::Project& project, te::Plugin& plugin, te::AudioTrack* source)
    {
        if (source == getSource (plugin))
            return true;

        if (source != nullptr && ! getPossibleSources (project, plugin).contains (source))
            return false;

        project.beginGesture (source != nullptr ? "Sidechain from " + source->getName() : juce::String ("Sidechain off"));

        // The wires first: setting the source rebuilds the playback with them.
        if (source != nullptr && plugin.getNumWires() == 0)
            plugin.guessSidechainRouting();

        plugin.setSidechainSourceID (source != nullptr ? source->itemID : te::EditItemID());

        if (auto* compressor = dynamic_cast<te::CompressorPlugin*> (&plugin))
            compressor->useSidechainTrigger = source != nullptr;

        return true;
    }

    void forgetSource (te::AudioTrack& track)
    {
        for (auto* other : te::getAudioTracks (track.edit))
        {
            for (auto* plugin : other->pluginList)
            {
                if (getSource (*plugin) == &track)
                {
                    plugin->setSidechainSourceID ({});

                    if (auto* compressor = dynamic_cast<te::CompressorPlugin*> (plugin))
                        compressor->useSidechainTrigger = false;
                }
            }
        }
    }

    juce::Array<te::AudioTrack*> getSourcesFor (const juce::Array<te::AudioTrack*>& tracks)
    {
        juce::Array<te::AudioTrack*> sources;
        auto toVisit = tracks;

        while (! toVisit.isEmpty())
        {
            auto* track = toVisit.removeAndReturn (toVisit.size() - 1);

            for (auto* plugin : track->pluginList)
            {
                if (auto* source = getSource (*plugin);
                    source != nullptr && ! tracks.contains (source) && ! sources.contains (source))
                {
                    sources.add (source);
                    toVisit.add (source);
                }
            }
        }

        return sources;
    }
}
