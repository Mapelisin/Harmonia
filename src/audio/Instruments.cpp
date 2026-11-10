#include "audio/Instruments.h"
#include "audio/DrumKit.h"
#include "audio/Sampler.h"
#include "audio/Effects.h"
#include "core/Drums.h"
#include "core/SongFiles.h"
#include "core/Tracks.h"

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

    juce::String useDrumKit (te::AudioTrack& track)
    {
        return replaceWith (track, track.edit.getPluginCache().createNewPlugin (drum_kit::Plugin::xmlTypeName, {}));
    }

    bool playsDrumKit (te::AudioTrack& track)
    {
        return dynamic_cast<drum_kit::Plugin*> (get (track)) != nullptr;
    }

    juce::String useSampler (te::AudioTrack& track, const juce::File& sound)
    {
        // Before anything changes: can the sound be used at all?
        if (sound != juce::File())
            if (const auto error = SampleData::check (track.edit.engine, sound); error.isNotEmpty())
                return error;

        if (const auto error = replaceWith (track, track.edit.getPluginCache().createNewPlugin (sampler::Plugin::xmlTypeName, {}));
            error.isNotEmpty())
            return error;

        auto* instrument = dynamic_cast<sampler::Plugin*> (get (track));

        if (instrument != nullptr && sound != juce::File())
        {
            // In the same undo step as the instrument: the caller's.
            instrument->state.setProperty (core::song_files::ids::file, sound.getFullPathName(), &track.edit.getUndoManager());
            instrument->updateSampleNow();
        }

        return {};
    }

    bool playsSampler (te::AudioTrack& track)
    {
        return dynamic_cast<sampler::Plugin*> (get (track)) != nullptr;
    }

    bool isUntouched (te::AudioTrack& track)
    {
        if (core::tracks::isAudio (track) || core::tracks::isBus (track))
            return false;

        if (auto* instrument = get (track); instrument != nullptr && dynamic_cast<te::FourOscPlugin*> (instrument) == nullptr)
            return false;

        for (auto* clip : track.getClips())
        {
            auto* midi = dynamic_cast<te::MidiClip*> (clip);

            if (midi == nullptr || ! midi->getSequence().getNotes().isEmpty())
                return false;
        }

        return true;
    }

    juce::String makeDrumTrack (te::AudioTrack& track)
    {
        if (auto error = useDrumKit (track); error.isNotEmpty())
            return error;

        core::drums::setShowsNames (track, true);

        // A track still called "Track 2" is the drums now.
        const auto name = track.getName();
        const auto number = name.fromFirstOccurrenceOf ("Track ", false, false);

        if (name.startsWith ("Track ") && number.isNotEmpty() && number.containsOnly ("0123456789"))
        {
            for (auto* clip : track.getClips())
                if (clip->getName() == name)
                    clip->setName ("Drums");

            track.setName ("Drums");
        }

        return {};
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
            effects::showEditor (*plugin);
    }
}
