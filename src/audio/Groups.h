#pragma once

#include "core/Project.h"

/** Group tracks: one fader, and one set of effects, for several tracks (all the drums).

    A group track (core::tracks::Kind::group) has no clips. The tracks in it play into it instead
    of into the master (Tracktion's track output), and it plays into the master through its effects
    and fader. Muting the group silences its tracks; soloing it solos them, and a soloed track keeps
    its group heard (Tracktion does both). A song track is in one group at most; groups don't go
    into other groups. Group tracks sit after the song's tracks and show only in the mixer. */
namespace harmonia::audio::groups
{
    /** Adds a group track with these song tracks in it (others are left out), named `name` or
        "Group" ("Group 2"...); one undo step. nullptr if the track could not be added. */
    te::AudioTrack* addGroup (core::Project&, const juce::Array<te::AudioTrack*>& tracks, const juce::String& name = {});

    /** Deletes a group track; its tracks play into the master again. One undo step. */
    void removeGroup (core::Project&, te::AudioTrack& group);

    /** One undo step. */
    void renameGroup (core::Project&, te::AudioTrack& group, const juce::String& name);

    /** The group the track plays into, or nullptr (the master). */
    te::AudioTrack* getGroup (const te::AudioTrack&);

    /** The song tracks in the group, top to bottom. */
    juce::Array<te::AudioTrack*> getTracks (const core::Project&, const te::AudioTrack& group);

    /** Puts a song track into a group, or back on the master (nullptr). Part of the current undo
        transaction (the caller starts it). Return and group tracks stay where they are. */
    void setGroup (te::AudioTrack& track, te::AudioTrack* group);
}
