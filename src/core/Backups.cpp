#include "core/Backups.h"

namespace harmonia::core::backups
{
    namespace
    {
        const char* const timeFormat = "%Y-%m-%d %H-%M-%S";

        /** The time a version's name ends with ("2026-11-10 14-03-22"), if it is one. */
        std::optional<juce::Time> parseTime (const juce::String& text)
        {
            if (text.length() != 19 || text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != '-' || text[16] != '-')
                return std::nullopt;

            const auto number = [&text] (int start, int length) { return text.substring (start, start + length); };

            for (const auto& part : { number (0, 4), number (5, 2), number (8, 2), number (11, 2), number (14, 2), number (17, 2) })
                if (! part.containsOnly ("0123456789"))
                    return std::nullopt;

            return juce::Time (number (0, 4).getIntValue(), number (5, 2).getIntValue() - 1, number (8, 2).getIntValue(),
                               number (11, 2).getIntValue(), number (14, 2).getIntValue(), number (17, 2).getIntValue());
        }

        juce::File fileFor (const juce::File& projectFile, juce::Time when)
        {
            return getFolder (projectFile).getChildFile (projectFile.getFileNameWithoutExtension() + " " + when.formatted (timeFormat)
                                                           + projectFile.getFileExtension());
        }

        /** Copies what the project file has now into the folder of versions; `kept` is the copy
            (none: there was nothing to keep, or the latest version is the same already). */
        juce::String keepCopy (const juce::File& projectFile, juce::File& kept)
        {
            kept = juce::File();

            if (! projectFile.existsAsFile() || projectFile.getSize() == 0)
                return {};

            const auto versions = list (projectFile);

            if (! versions.empty() && versions.front().file.hasIdenticalContentTo (projectFile))
                return {};

            const auto folder = getFolder (projectFile);

            if (! folder.createDirectory())
                return "Cannot create folder: " + folder.getFullPathName();

            // Named by when that version was saved (to the second); never earlier than the latest
            // one kept (two saves within a second, a clock set back).
            auto when = projectFile.getLastModificationTime();

            if (when.toMilliseconds() <= 0)
                when = juce::Time::getCurrentTime();

            when = juce::Time ((when.toMilliseconds() / 1000) * 1000);

            if (! versions.empty() && when <= versions.front().savedAt)
                when = versions.front().savedAt + juce::RelativeTime::seconds (1.0);

            auto target = fileFor (projectFile, when);

            while (target.exists())
            {
                when += juce::RelativeTime::seconds (1.0);
                target = fileFor (projectFile, when);
            }

            if (! projectFile.copyFileTo (target))
                return "Cannot keep the earlier version as " + target.getFullPathName();

            kept = target;
            return {};
        }

        /** Drops the oldest versions beyond maxVersions, and the folder if nothing is left in it. */
        void thin (const juce::File& projectFile)
        {
            const auto all = list (projectFile);

            for (auto i = (size_t) maxVersions; i < all.size(); ++i)
                all[i].file.deleteFile();

            if (const auto folder = getFolder (projectFile);
                folder.isDirectory() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
                folder.deleteFile();
        }
    }

    juce::File getFolder (const juce::File& projectFile)
    {
        if (projectFile == juce::File())
            return {};

        return projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + " Backups");
    }

    juce::String Version::getLabel() const
    {
        return savedAt.formatted ("%d %b %Y  %H:%M:%S");
    }

    std::vector<Version> list (const juce::File& projectFile)
    {
        std::vector<Version> result;

        if (projectFile == juce::File())
            return result;

        const auto folder = getFolder (projectFile);
        const auto prefix = projectFile.getFileNameWithoutExtension() + " ";

        if (! folder.isDirectory())
            return result;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*" + projectFile.getFileExtension(), juce::File::findFiles))
        {
            const auto name = entry.getFile().getFileNameWithoutExtension();

            if (! name.startsWith (prefix))
                continue;

            if (const auto when = parseTime (name.substring (prefix.length())))
                result.push_back ({ entry.getFile(), *when });
        }

        std::sort (result.begin(), result.end(), [] (const Version& a, const Version& b) { return a.savedAt > b.savedAt; });
        return result;
    }

    juce::String keep (const juce::File& projectFile)
    {
        juce::File kept;
        const auto error = keepCopy (projectFile, kept);
        thin (projectFile);
        return error;
    }

    juce::String save (Project& project, const juce::File& file, juce::String& warning)
    {
        juce::File kept;
        warning = keepCopy (file, kept);
        const auto error = project.save (file);

        // Saved with nothing new in it: the version just kept is the song as it is, not an earlier one.
        if (error.isEmpty() && kept != juce::File() && kept.hasIdenticalContentTo (file))
            kept.deleteFile();

        thin (file);
        return error;
    }

    std::unique_ptr<Project> open (Session& session, const Version& version, const juce::File& projectFile, juce::String& error)
    {
        return Project::loadRecovered (session, version.file, projectFile, error);
    }
}
