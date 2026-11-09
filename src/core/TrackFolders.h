#pragma once

#include "core/Tracktion.h"

/** Track folders: a run of neighbouring song tracks kept together under a name. A folder can be
    collapsed (the arrangement then shows it as one row), and muted or soloed as a whole.

    A folder is not a track: it plays nothing and has no fader (a group track is for that, see
    audio::groups). The folders are kept in the song, each track remembers which one it is in.
    Changes go into the current undo step (the caller starts it); collapsing isn't an undo step.

    A folder's tracks are always next to each other: after tracks were moved, added or deleted,
    trackMoved() and tidy() put that right (a track moved away leaves its folder, one moved in
    between joins). */
namespace harmonia::core::folders
{
    struct Folder
    {
        juce::String id;            ///< identifies it (never shown)
        juce::String name;
        bool collapsed = false;

        bool operator== (const Folder&) const = default;
    };

    /** Every folder, in the order of their first tracks in the song. */
    std::vector<Folder> getAll (const te::Edit&);

    /** The folder a track is in, if any. */
    std::optional<Folder> getFolderOf (const te::Track&);

    /** A folder's tracks, in song order. */
    juce::Array<te::AudioTrack*> getMembers (const te::Edit&, const juce::String& folderId);

    /** A new folder of these song tracks (return and group tracks left out): they are brought
        together where the first of them is. Nothing if there are no tracks. */
    std::optional<Folder> add (te::Edit&, const juce::Array<te::AudioTrack*>& tracks, const juce::String& name);

    /** Puts a track into a folder, after its last track; an empty ID takes it out (it moves
        below the folder it was in). */
    void setFolder (te::AudioTrack&, const juce::String& folderId);

    void rename (te::Edit&, const juce::String& folderId, const juce::String& name);

    /** Not an undo step: how the song is looked at. */
    void setCollapsed (te::Edit&, const juce::String& folderId, bool);

    /** Forgets the folder: its tracks stay where they are. */
    void remove (te::Edit&, const juce::String& folderId);

    /** After tracks were added or deleted (or several moved): a track away from the run of its
        folder's tracks that starts at the first of them leaves it, a folder without tracks goes. */
    void tidy (te::Edit&);

    /** After a track was moved: between two tracks of one folder it joins that folder; away
        from the tracks of its own it leaves it. Then tidy(). */
    void trackMoved (te::AudioTrack&);

    /** Every track of the folder muted (soloed)? */
    bool isMuted (const te::Edit&, const juce::String& folderId);
    bool isSoloed (const te::Edit&, const juce::String& folderId);

    /** Like a track's own mute and solo, not undo steps. */
    void setMuted (te::Edit&, const juce::String& folderId, bool);
    void setSoloed (te::Edit&, const juce::String& folderId, bool);

    /** "Folder", "Folder 2"...: a name no folder has yet. */
    juce::String getNewName (const te::Edit&);
}
