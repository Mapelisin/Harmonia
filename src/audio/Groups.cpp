#include "audio/Groups.h"
#include "audio/Sidechain.h"

namespace harmonia::audio::groups
{
    te::AudioTrack* addGroup (core::Project& project, const juce::Array<te::AudioTrack*>& tracks, const juce::String& name)
    {
        project.beginGesture ("Add group");
        auto* group = project.addGroupTrack (core::tracks::getUniqueBusName (project.getEdit(), name.trim().isNotEmpty() ? name.trim() : "Group"));

        if (group == nullptr)
            return nullptr;

        for (auto* track : tracks)
            if (track != nullptr)
                setGroup (*track, group);

        return group;
    }

    void removeGroup (core::Project& project, te::AudioTrack& group)
    {
        if (! core::tracks::isGroup (group))
            return;

        const te::Track::Ptr keepAlive (&group);
        project.beginGesture ("Delete group " + group.getName());

        // Its tracks back on the master first: once it's gone, they would play into nothing.
        for (auto* track : getTracks (project, group))
            setGroup (*track, nullptr);

        sidechain::forgetSource (group);
        project.getEdit().deleteTrack (&group);
    }

    void renameGroup (core::Project& project, te::AudioTrack& group, const juce::String& newName)
    {
        const auto name = newName.trim();

        if (name.isEmpty() || name == group.getName() || ! core::tracks::isGroup (group))
            return;

        project.beginGesture ("Rename group");
        group.setName (name);
    }

    te::AudioTrack* getGroup (const te::AudioTrack& track)
    {
        auto* destination = track.getOutput().getDestinationTrack();
        return destination != nullptr && core::tracks::isGroup (*destination) ? destination : nullptr;
    }

    juce::Array<te::AudioTrack*> getTracks (const core::Project& project, const te::AudioTrack& group)
    {
        juce::Array<te::AudioTrack*> inGroup;

        for (auto* track : project.getTracks())
            if (getGroup (*track) == &group)
                inGroup.add (track);

        return inGroup;
    }

    void setGroup (te::AudioTrack& track, te::AudioTrack* group)
    {
        if (core::tracks::isBus (track) || getGroup (track) == group)
            return;

        if (group != nullptr && core::tracks::isGroup (*group))
            track.getOutput().setOutputToTrack (group);
        else
            track.getOutput().setOutputToDefaultDevice (false);
    }
}
