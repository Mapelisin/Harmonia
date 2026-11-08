#include "audio/Sends.h"
#include "core/Tracks.h"

#include <set>

namespace harmonia::audio::sends
{
    namespace
    {
        te::AuxReturnPlugin* getReturnPlugin (const te::AudioTrack& track)
        {
            if (! core::tracks::isReturn (track))
                return nullptr;

            for (auto* plugin : track.pluginList)
                if (auto* returnPlugin = dynamic_cast<te::AuxReturnPlugin*> (plugin))
                    return returnPlugin;

            return nullptr;
        }

        /** The lowest bus no return track listens to and no track sends to (a send left over
            from an older project must not wake up on a new return). */
        int findFreeBus (te::Edit& edit)
        {
            std::set<int> used;

            for (auto* track : te::getAudioTracks (edit))
            {
                for (auto* plugin : track->pluginList)
                {
                    if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
                        used.insert (send->busNumber.get());
                    else if (auto* returnPlugin = dynamic_cast<te::AuxReturnPlugin*> (plugin))
                        used.insert (returnPlugin->busNumber.get());
                }
            }

            for (int bus = 0; bus < maxReturnTracks; ++bus)
                if (used.count (bus) == 0)
                    return bus;

            return -1;
        }

        juce::String getUniqueName (core::Project& project, const juce::String& base)
        {
            const auto taken = [&project] (const juce::String& name)
            {
                for (auto* track : project.getReturnTracks())
                    if (track->getName() == name)
                        return true;

                return false;
            };

            auto name = base;

            for (int i = 2; taken (name); ++i)
                name = base + " " + juce::String (i);

            return name;
        }

        juce::String getPresetName (Preset preset)
        {
            switch (preset)
            {
                case Preset::reverb:    return "Reverb";
                case Preset::delay:     return "Delay";
                case Preset::empty:     break;
            }

            return "Return";
        }

        /** The preset's effect, set up to give only the effect (no dry sound: that comes from the
            tracks themselves). */
        te::Plugin::Ptr createPresetEffect (core::Project& project, Preset preset)
        {
            auto& edit = project.getEdit();

            if (preset == Preset::reverb)
            {
                auto plugin = edit.getPluginCache().createNewPlugin (te::ReverbPlugin::xmlTypeName, {});

                if (auto* reverb = dynamic_cast<te::ReverbPlugin*> (plugin.get()))
                {
                    reverb->dryParam->setParameter (0.0f, juce::sendNotification);
                    reverb->roomSizeParam->setParameter (0.6f, juce::sendNotification);
                }

                return plugin;
            }

            if (preset == Preset::delay)
            {
                auto plugin = edit.getPluginCache().createNewPlugin (te::DelayPlugin::xmlTypeName, {});

                if (auto* delay = dynamic_cast<te::DelayPlugin*> (plugin.get()))
                {
                    delay->mixProportion->setParameter (1.0f, juce::sendNotification);
                    // A dotted eighth at the song's tempo: the classic echo.
                    delay->lengthMs = juce::jlimit (1, 2000, juce::roundToInt (60000.0 / project.getTempoBpm() * 0.75));
                }

                return plugin;
            }

            return {};
        }

        int getSendInsertIndex (te::AudioTrack& track)
        {
            // After the fader, before the level meter.
            if (auto* meter = track.getLevelMeterPlugin())
                if (const auto index = track.pluginList.indexOf (meter); index >= 0)
                    return index;

            if (auto* volume = track.getVolumePlugin())
                if (const auto index = track.pluginList.indexOf (volume); index >= 0)
                    return index + 1;

            return -1;
        }
    }

    te::AudioTrack* addReturnTrack (core::Project& project, Preset preset, juce::String& error)
    {
        error = {};
        auto& edit = project.getEdit();
        const auto bus = findFreeBus (edit);

        if (bus < 0)
        {
            error = "A project can have up to " + juce::String (maxReturnTracks) + " return tracks";
            return nullptr;
        }

        const auto name = getUniqueName (project, getPresetName (preset));
        project.beginGesture ("Add " + name + " return");
        auto* track = project.addReturnTrack (name);

        if (track == nullptr)
        {
            error = "The return track could not be added";
            return nullptr;
        }

        juce::ValueTree returnState (te::IDs::PLUGIN);
        returnState.setProperty (te::IDs::type, te::AuxReturnPlugin::xmlTypeName, nullptr);
        returnState.setProperty (te::IDs::busNum, bus, nullptr);

        if (auto returnPlugin = edit.getPluginCache().createNewPlugin (returnState))
            track->pluginList.insertPlugin (returnPlugin, 0, nullptr);

        if (auto effect = createPresetEffect (project, preset))
            track->pluginList.insertPlugin (effect, track->pluginList.indexOf (track->getVolumePlugin()), nullptr);

        edit.setAuxBusName (bus, name);
        track->setSoloIsolate (true);
        return track;
    }

    void removeReturnTrack (core::Project& project, te::AudioTrack& returnTrack)
    {
        const te::Track::Ptr keepAlive (&returnTrack);
        auto& edit = project.getEdit();
        const auto bus = getBus (returnTrack);
        project.beginGesture ("Delete " + returnTrack.getName() + " return");

        if (bus >= 0)
        {
            juce::Array<te::Plugin::Ptr> sendsToIt;

            for (auto* track : te::getAudioTracks (edit))
                for (auto* plugin : track->pluginList)
                    if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin); send != nullptr && send->busNumber.get() == bus)
                        sendsToIt.add (plugin);

            for (auto& send : sendsToIt)
                send->deleteFromParent();

            edit.setAuxBusName (bus, {});
        }

        edit.deleteTrack (&returnTrack);
    }

    void renameReturnTrack (core::Project& project, te::AudioTrack& returnTrack, const juce::String& newName)
    {
        const auto name = newName.trim();

        if (name.isEmpty() || name == returnTrack.getName())
            return;

        project.beginGesture ("Rename return track");
        returnTrack.setName (name);

        if (const auto bus = getBus (returnTrack); bus >= 0)
            project.getEdit().setAuxBusName (bus, name);
    }

    int getBus (const te::AudioTrack& returnTrack)
    {
        auto* plugin = getReturnPlugin (returnTrack);
        return plugin != nullptr ? plugin->busNumber.get() : -1;
    }

    te::AuxSendPlugin* getSend (const te::AudioTrack& track, const te::AudioTrack& returnTrack)
    {
        const auto bus = getBus (returnTrack);

        if (bus < 0)
            return nullptr;

        for (auto* plugin : track.pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin); send != nullptr && send->busNumber.get() == bus)
                return send;

        return nullptr;
    }

    float getLevel (const te::AudioTrack& track, const te::AudioTrack& returnTrack)
    {
        auto* send = getSend (track, returnTrack);
        return send != nullptr ? send->gain->getCurrentValue() : 0.0f;
    }

    float getLevelDb (const te::AudioTrack& track, const te::AudioTrack& returnTrack)
    {
        auto* send = getSend (track, returnTrack);
        return send != nullptr ? te::volumeFaderPositionToDB (send->gain->getCurrentValue()) : -100.0f;
    }

    bool setLevel (te::AudioTrack& track, te::AudioTrack& returnTrack, float faderPosition)
    {
        faderPosition = juce::jlimit (0.0f, 1.0f, faderPosition);
        const auto bus = getBus (returnTrack);

        if (bus < 0 || core::tracks::isReturn (track))
            return false;

        auto* send = getSend (track, returnTrack);

        if (send == nullptr)
        {
            if (faderPosition <= 0.0f)
                return true;   // nothing to send, nothing to add

            if (! track.pluginList.canInsertPlugin())
                return false;

            juce::ValueTree state (te::IDs::PLUGIN);
            state.setProperty (te::IDs::type, te::AuxSendPlugin::xmlTypeName, nullptr);
            state.setProperty (te::IDs::busNum, bus, nullptr);
            auto plugin = track.edit.getPluginCache().createNewPlugin (state);

            if (plugin == nullptr)
                return false;

            track.pluginList.insertPlugin (plugin, getSendInsertIndex (track), nullptr);
            send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());

            if (send == nullptr || ! track.pluginList.contains (send))
                return false;
        }

        send->gain->setParameter (faderPosition, juce::sendNotification);
        return true;
    }

    bool setLevelDb (te::AudioTrack& track, te::AudioTrack& returnTrack, float db)
    {
        return setLevel (track, returnTrack, db <= -100.0f ? 0.0f : te::decibelsToVolumeFaderPosition (db));
    }
}
