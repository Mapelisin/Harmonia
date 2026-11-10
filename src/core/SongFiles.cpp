#include "core/SongFiles.h"
#include "core/AudioClips.h"

namespace harmonia::core::song_files
{
    namespace
    {
        /** A search for missing files doesn't look at more files than this (a drive's root...). */
        constexpr int maxFilesSearched = 200000;

        juce::String trackNameOf (juce::ValueTree node)
        {
            for (; node.isValid(); node = node.getParent())
                if (te::TrackList::isTrack (node))
                    return node[te::IDs::name].toString();

            return {};
        }

        juce::String describeUse (const juce::ValueTree& node, Reference::Kind kind)
        {
            const auto track = trackNameOf (node);
            const auto onTrack = track.isNotEmpty() ? " on " + track : juce::String();

            switch (kind)
            {
                case Reference::Kind::clip:
                    return node[te::IDs::name].toString() + onTrack;

                case Reference::Kind::take:
                {
                    // TAKE > the takes > the clip.
                    const auto clip = node.getParent().getParent();
                    return "a take of " + clip[te::IDs::name].toString() + onTrack;
                }

                case Reference::Kind::drumSample:
                    return "a drum of " + (track.isNotEmpty() ? track : juce::String ("the drum machine"));

                case Reference::Kind::samplerSound:
                    return "the sampler of " + (track.isNotEmpty() ? track : juce::String ("a track"));
            }

            return {};
        }

        Reference::Kind kindOf (const juce::ValueTree& node)
        {
            if (node.hasType (te::IDs::AUDIOCLIP))  return Reference::Kind::clip;
            if (node.hasType (te::IDs::TAKE))       return Reference::Kind::take;
            if (node.hasType (ids::drumSample))     return Reference::Kind::drumSample;
            return Reference::Kind::samplerSound;
        }

        te::AudioClipBase* findAudioClip (te::Edit& edit, const juce::ValueTree& node)
        {
            for (auto* track : te::getAudioTracks (edit))
                for (auto* clip : track->getClips())
                    if (clip->state == node)
                        return dynamic_cast<te::AudioClipBase*> (clip);

            return nullptr;
        }

        /** Makes the place name `file` (into the current undo step). */
        void setFile (Project& project, const Reference& reference, const juce::File& file)
        {
            auto& edit = project.getEdit();
            auto node = reference.node;
            node.setProperty (reference.property, file.getFullPathName(), &edit.getUndoManager());

            // A clip plays the new file the way it played the old one.
            if (reference.kind == Reference::Kind::clip)
                if (auto* clip = findAudioClip (edit, node))
                    audio_clips::prepareForPlayback (*clip);
        }

        /** The file in `folder` that is this sound: one that is there already (the same content,
            under the sound's name or that name with a number), or a copy made now, with a number
            when another sound has the name. None: it couldn't be copied. */
        juce::File bringInto (const juce::File& sound, const juce::File& folder, bool& copied)
        {
            copied = false;

            if (! folder.createDirectory())
                return {};

            for (int number = 1; number < 1000; ++number)
            {
                const auto name = number == 1 ? sound.getFileName()
                                              : sound.getFileNameWithoutExtension() + " (" + juce::String (number) + ")"
                                                  + sound.getFileExtension();
                const auto target = folder.getChildFile (name);

                if (! target.exists())
                {
                    copied = sound.copyFileTo (target);
                    return copied ? target : juce::File();
                }

                if (target.existsAsFile() && target.hasIdenticalContentTo (sound))
                    return target;
            }

            return {};
        }

        juce::String megabytes (juce::int64 bytes)
        {
            return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
        }

        juce::String files (int count)
        {
            return juce::String (count) + (count == 1 ? " file" : " files");
        }
    }

    void forEachReference (juce::ValueTree tree, const std::function<void (juce::ValueTree&, const juce::Identifier&)>& fn)
    {
        if ((tree.hasType (te::IDs::AUDIOCLIP) || tree.hasType (te::IDs::TAKE)) && tree[te::IDs::source].toString().isNotEmpty())
            fn (tree, te::IDs::source);
        else if (tree.hasType (ids::drumSample) && tree[ids::file].toString().isNotEmpty())
            fn (tree, ids::file);
        else if (tree.hasType (te::IDs::PLUGIN) && tree[te::IDs::type].toString() == ids::samplerType
                   && tree[ids::file].toString().isNotEmpty())
            fn (tree, ids::file);

        for (auto child : tree)
            forEachReference (child, fn);
    }

    juce::File resolve (const te::Edit& edit, const juce::String& path)
    {
        if (path.isEmpty())
            return {};

        return te::SourceFileReference::findFileFromString (const_cast<te::Edit&> (edit), path);
    }

    std::vector<Reference> getAll (te::Edit& edit)
    {
        std::vector<Reference> result;

        forEachReference (edit.state, [&edit, &result] (juce::ValueTree& node, const juce::Identifier& property)
        {
            Reference reference;
            reference.kind = kindOf (node);
            reference.node = node;
            reference.property = property;
            reference.file = resolve (edit, node[property].toString());
            reference.usedBy = describeUse (node, reference.kind);

            // A path that leads nowhere at all (an ID of a library this song never had) is kept
            // as it is written, to be named as missing.
            if (reference.file == juce::File() && juce::File::isAbsolutePath (node[property].toString()))
                reference.file = juce::File (node[property].toString());

            if (reference.file != juce::File())
                result.push_back (reference);
        });

        return result;
    }

    std::vector<Missing> getMissing (te::Edit& edit)
    {
        std::vector<Missing> result;

        for (const auto& reference : getAll (edit))
        {
            if (! reference.isMissing())
                continue;

            auto found = std::find_if (result.begin(), result.end(), [&reference] (const Missing& m) { return m.file == reference.file; });

            if (found == result.end())
            {
                result.push_back ({ reference.file, {} });
                found = result.end() - 1;
            }

            found->usedBy.addIfNotAlreadyThere (reference.usedBy);
        }

        std::sort (result.begin(), result.end(), [] (const Missing& a, const Missing& b)
        {
            return a.file.getFileName().compareNatural (b.file.getFileName()) < 0;
        });

        return result;
    }

    //==============================================================================
    juce::File getFolder (const juce::File& projectFile)
    {
        if (projectFile == juce::File())
            return {};

        return projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + " Audio");
    }

    juce::String Collected::describe() const
    {
        juce::StringArray parts;

        if (copied > 0)
            parts.add ("Copied " + files (copied) + " (" + megabytes (bytes) + ") into " + folder.getFullPathName());
        else if (changed > 0)
            parts.add ("The song uses the copies in " + folder.getFullPathName() + " now");
        else if (problems.isEmpty() && missing == 0)
            parts.add (already > 0 ? "All the song's sound files are in " + folder.getFullPathName() + " already"
                                   : juce::String ("The song uses no sound files"));

        if (copied > 0 && already > 0)
            parts.add (juce::String (already) + (already == 1 ? " was" : " were") + " there already");

        if (missing > 0)
            parts.add (juce::String (missing) + (missing == 1 ? " is" : " are") + " missing (File > Find missing files)");

        if (! problems.isEmpty())
            parts.add ("couldn't copy " + problems.joinIntoString (", "));

        return parts.joinIntoString ("; ");
    }

    Collected collect (Project& project, const juce::File& projectFile)
    {
        Collected result;
        result.folder = getFolder (projectFile);
        auto& edit = project.getEdit();

        // A song that was never saved has no folder to collect into.
        if (result.folder == juce::File())
        {
            result.problems.add ("the song isn't saved yet");
            return result;
        }

        struct Brought { juce::File from, to; };
        std::vector<Brought> brought;                               // each sound once
        std::vector<std::pair<Reference, juce::File>> changes;
        juce::Array<juce::File> seen;

        for (const auto& reference : getAll (edit))
        {
            const auto first = seen.addIfNotAlreadyThere (reference.file);

            if (reference.isMissing())
            {
                result.missing += first ? 1 : 0;
                continue;
            }

            if (reference.file.isAChildOf (result.folder))
            {
                result.already += first ? 1 : 0;
                continue;
            }

            auto known = std::find_if (brought.begin(), brought.end(), [&reference] (const Brought& b) { return b.from == reference.file; });

            if (known == brought.end())
            {
                auto copied = false;
                const auto target = bringInto (reference.file, result.folder, copied);

                if (target == juce::File())
                {
                    result.problems.addIfNotAlreadyThere (reference.file.getFileName());
                    continue;
                }

                if (copied)
                {
                    ++result.copied;
                    result.bytes += target.getSize();
                }
                else
                {
                    ++result.already;
                }

                brought.push_back ({ reference.file, target });
                known = brought.end() - 1;
            }

            changes.push_back ({ reference, known->to });
        }

        result.changed = (int) changes.size();

        if (! changes.empty())
        {
            project.beginGesture ("Collect files");

            for (const auto& [reference, target] : changes)
                setFile (project, reference, target);
        }

        return result;
    }

    Found findIn (Project& project, const juce::File& folder)
    {
        Found result;
        const auto missing = getMissing (project.getEdit());

        if (missing.empty() || ! folder.isDirectory())
        {
            result.stillMissing = (int) missing.size();
            return result;
        }

        // The first file of each wanted name, wherever under the folder it is.
        std::vector<juce::File> found (missing.size());
        auto left = (int) missing.size();
        auto searched = 0;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
        {
            if (left == 0 || ++searched > maxFilesSearched)
                break;

            const auto name = entry.getFile().getFileName();

            for (size_t i = 0; i < missing.size(); ++i)
            {
                if (found[i] == juce::File() && missing[i].file.getFileName().equalsIgnoreCase (name))
                {
                    found[i] = entry.getFile();
                    --left;
                }
            }
        }

        result.stillMissing = left;
        result.found = (int) missing.size() - left;

        if (result.found > 0)
        {
            project.beginGesture ("Find missing files");

            for (size_t i = 0; i < missing.size(); ++i)
                if (found[i] != juce::File())
                    repoint (project, missing[i].file, found[i]);
        }

        return result;
    }

    int repoint (Project& project, const juce::File& from, const juce::File& to)
    {
        int count = 0;

        for (const auto& reference : getAll (project.getEdit()))
        {
            if (reference.file == from)
            {
                setFile (project, reference, to);
                ++count;
            }
        }

        return count;
    }
}
