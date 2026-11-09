#include "core/TrackFolders.h"
#include "core/Tracks.h"

namespace harmonia::core::folders
{
    namespace
    {
        const juce::Identifier foldersTag ("HFOLDERS");         // in the song's state
        const juce::Identifier folderTag ("FOLDER");
        const juce::Identifier idId ("id"), nameId ("name"), collapsedId ("collapsed");
        const juce::Identifier trackFolderId ("hFolder");       // on a track's node

        /** The song's tracks (no return or group tracks), in order. */
        juce::Array<te::AudioTrack*> getSongTracks (const te::Edit& edit)
        {
            auto tracks = te::getAudioTracks (edit);
            tracks.removeIf ([] (te::AudioTrack* track) { return tracks::isBus (*track); });
            return tracks;
        }

        juce::String folderIdOf (const te::Track& track)
        {
            return track.state[trackFolderId].toString();
        }

        juce::ValueTree findNode (const te::Edit& edit, const juce::String& folderId)
        {
            return folderId.isEmpty() ? juce::ValueTree()
                                      : edit.state.getChildWithName (foldersTag).getChildWithProperty (idId, folderId);
        }

        Folder read (const juce::ValueTree& node)
        {
            return { node[idId].toString(), node[nameId].toString(), (bool) node[collapsedId] };
        }

        void setFolderId (te::Track& track, const juce::String& folderId)
        {
            auto* undoManager = &track.edit.getUndoManager();

            if (folderId.isEmpty())
                track.state.removeProperty (trackFolderId, undoManager);
            else
                track.state.setProperty (trackFolderId, folderId, undoManager);
        }

        void moveAfter (te::AudioTrack& track, te::Track& preceding)
        {
            if (&track != &preceding)
                track.edit.moveTrack (&track, te::TrackInsertPoint (nullptr, &preceding));
        }
    }

    std::vector<Folder> getAll (const te::Edit& edit)
    {
        std::vector<Folder> folders;
        juce::StringArray seen;

        // In the order of their first tracks.
        for (auto* track : getSongTracks (edit))
            if (const auto id = folderIdOf (*track); id.isNotEmpty() && ! seen.contains (id))
                if (const auto node = findNode (edit, id); node.isValid())
                {
                    seen.add (id);
                    folders.push_back (read (node));
                }

        return folders;
    }

    std::optional<Folder> getFolderOf (const te::Track& track)
    {
        const auto node = findNode (track.edit, folderIdOf (track));
        return node.isValid() ? std::optional<Folder> (read (node)) : std::nullopt;
    }

    juce::Array<te::AudioTrack*> getMembers (const te::Edit& edit, const juce::String& folderId)
    {
        juce::Array<te::AudioTrack*> members;

        if (findNode (edit, folderId).isValid())
            for (auto* track : getSongTracks (edit))
                if (folderIdOf (*track) == folderId)
                    members.add (track);

        return members;
    }

    juce::String getNewName (const te::Edit& edit)
    {
        juce::StringArray used;

        for (const auto& node : edit.state.getChildWithName (foldersTag))
            used.add (node[nameId].toString());

        if (! used.contains ("Folder"))
            return "Folder";

        for (int number = 2;; ++number)
            if (const auto name = "Folder " + juce::String (number); ! used.contains (name))
                return name;
    }

    std::optional<Folder> add (te::Edit& edit, const juce::Array<te::AudioTrack*>& wanted, const juce::String& name)
    {
        // In song order, whatever order they were given in.
        juce::Array<te::AudioTrack*> tracks;

        for (auto* track : getSongTracks (edit))
            if (wanted.contains (track))
                tracks.add (track);

        if (tracks.isEmpty())
            return std::nullopt;

        auto* undoManager = &edit.getUndoManager();
        const Folder folder { juce::Uuid().toString(), name.trim().isNotEmpty() ? name.trim() : getNewName (edit), false };

        edit.state.getOrCreateChildWithName (foldersTag, undoManager)
            .appendChild (juce::ValueTree (folderTag, { { idId, folder.id }, { nameId, folder.name } }), undoManager);

        // Together, from where the first is.
        setFolderId (*tracks.getFirst(), folder.id);

        for (int i = 1; i < tracks.size(); ++i)
        {
            moveAfter (*tracks[i], *tracks[i - 1]);
            setFolderId (*tracks[i], folder.id);
        }

        tidy (edit);
        return folder;
    }

    void setFolder (te::AudioTrack& track, const juce::String& folderId)
    {
        auto& edit = track.edit;
        const auto current = folderIdOf (track);

        if (current == folderId || tracks::isBus (track) || (folderId.isNotEmpty() && ! findNode (edit, folderId).isValid()))
            return;

        // Next to where it belongs: after the folder's last track, or out below the one it leaves.
        auto neighbours = getMembers (edit, folderId.isNotEmpty() ? folderId : current);
        neighbours.removeFirstMatchingValue (&track);
        setFolderId (track, folderId);

        if (! neighbours.isEmpty())
            moveAfter (track, *neighbours.getLast());

        tidy (edit);
    }

    void rename (te::Edit& edit, const juce::String& folderId, const juce::String& name)
    {
        if (auto node = findNode (edit, folderId); node.isValid() && name.trim().isNotEmpty())
            node.setProperty (nameId, name.trim(), &edit.getUndoManager());
    }

    void setCollapsed (te::Edit& edit, const juce::String& folderId, bool collapsed)
    {
        if (auto node = findNode (edit, folderId); node.isValid())
            node.setProperty (collapsedId, collapsed, nullptr);
    }

    void remove (te::Edit& edit, const juce::String& folderId)
    {
        auto node = findNode (edit, folderId);

        if (! node.isValid())
            return;

        for (auto* track : getMembers (edit, folderId))
            setFolderId (*track, {});

        node.getParent().removeChild (node, &edit.getUndoManager());
    }

    void tidy (te::Edit& edit)
    {
        auto* undoManager = &edit.getUndoManager();
        const auto tracks = getSongTracks (edit);

        // A folder is the run of its tracks that starts at the first of them: the others left it.
        juce::StringArray closed;

        for (int i = 0; i < tracks.size(); ++i)
        {
            const auto id = folderIdOf (*tracks[i]);

            if (id.isEmpty())
                continue;

            const auto follows = i > 0 && folderIdOf (*tracks[i - 1]) == id;

            if (! findNode (edit, id).isValid() || (closed.contains (id) && ! follows))
                setFolderId (*tracks[i], {});
            else
                closed.addIfNotAlreadyThere (id);
        }

        // Folders without tracks.
        auto nodes = edit.state.getChildWithName (foldersTag);

        for (int i = nodes.getNumChildren(); --i >= 0;)
            if (getMembers (edit, nodes.getChild (i)[idId].toString()).isEmpty())
                nodes.removeChild (i, undoManager);
    }

    void trackMoved (te::AudioTrack& track)
    {
        auto& edit = track.edit;
        const auto tracks = getSongTracks (edit);
        const auto index = tracks.indexOf (&track);

        if (index >= 0)
        {
            const auto above = index > 0 ? folderIdOf (*tracks[index - 1]) : juce::String();
            const auto below = index + 1 < tracks.size() ? folderIdOf (*tracks[index + 1]) : juce::String();
            const auto own = folderIdOf (track);

            // Between two tracks of one folder: one of them. Away from its own: out of it.
            if (above.isNotEmpty() && above == below)
                setFolderId (track, above);
            else if (own.isNotEmpty() && above != own && below != own && getMembers (edit, own).size() > 1)
                setFolderId (track, {});
        }

        tidy (edit);
    }

    bool isMuted (const te::Edit& edit, const juce::String& folderId)
    {
        const auto members = getMembers (edit, folderId);
        return ! members.isEmpty() && std::all_of (members.begin(), members.end(), [] (te::AudioTrack* t) { return t->isMuted (false); });
    }

    bool isSoloed (const te::Edit& edit, const juce::String& folderId)
    {
        const auto members = getMembers (edit, folderId);
        return ! members.isEmpty() && std::all_of (members.begin(), members.end(), [] (te::AudioTrack* t) { return t->isSolo (false); });
    }

    void setMuted (te::Edit& edit, const juce::String& folderId, bool muted)
    {
        for (auto* track : getMembers (edit, folderId))
            track->setMute (muted);
    }

    void setSoloed (te::Edit& edit, const juce::String& folderId, bool soloed)
    {
        for (auto* track : getMembers (edit, folderId))
            track->setSolo (soloed);
    }
}
