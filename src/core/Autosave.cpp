#include "core/Autosave.h"

namespace harmonia::core
{
    namespace
    {
        const juce::Identifier infoTag ("HARMONIA_AUTOSAVE");
        const juce::Identifier originalId ("original");
        const juce::Identifier savedAtId ("savedAt");
    }

    Autosave::Autosave (const juce::File& f)
        : folder (f)
    {
    }

    juce::File Autosave::getDefaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Harmonia").getChildFile ("Autosave");
    }

    juce::File Autosave::getCopyFile() const        { return folder.getChildFile ("Unsaved work.harmonia"); }
    juce::File Autosave::getDiscardedFile() const   { return folder.getChildFile ("Discarded work.harmonia"); }
    juce::File Autosave::getInfoFile() const        { return folder.getChildFile ("Unsaved work.xml"); }

    juce::String Autosave::update (Project& project, const juce::File& original, juce::Time now)
    {
        if (! project.hasUnsavedChanges())
        {
            if (getCopyFile().exists() || getInfoFile().exists())
                clear();

            copiedChange.reset();
            return {};
        }

        const auto change = project.getChangeCount();

        if (copiedChange == change)
            return {};

        // Changes keep coming: not more often than every interval.
        if (copiedChange.has_value() && now < lastCopy + juce::RelativeTime::milliseconds (defaultIntervalMs))
            return {};

        if (! folder.createDirectory())
            return "Can't create the autosave folder " + folder.getFullPathName();

        if (const auto error = project.saveCopy (getCopyFile()); error.isNotEmpty())
            return error;

        juce::XmlElement info (infoTag);
        info.setAttribute (originalId, original.getFullPathName());
        info.setAttribute (savedAtId, juce::String (now.toMilliseconds()));

        if (! info.writeTo (getInfoFile()))
            return "Can't write " + getInfoFile().getFullPathName();

        copiedChange = change;
        lastCopy = now;
        return {};
    }

    void Autosave::projectReplaced()
    {
        copiedChange.reset();
        lastCopy = {};
    }

    void Autosave::clear()
    {
        getCopyFile().deleteFile();
        getInfoFile().deleteFile();
        copiedChange.reset();
    }

    //==============================================================================
    juce::String Autosave::Leftover::getName() const
    {
        return original != juce::File() ? original.getFileNameWithoutExtension() : juce::String ("Untitled");
    }

    std::optional<Autosave::Leftover> Autosave::findLeftover() const
    {
        if (! getCopyFile().existsAsFile())
            return {};

        Leftover leftover;
        leftover.copy = getCopyFile();
        leftover.savedAt = leftover.copy.getLastModificationTime();

        if (const auto info = juce::parseXML (getInfoFile()); info != nullptr && info->hasTagName (infoTag.toString()))
        {
            if (const auto path = info->getStringAttribute (originalId); juce::File::isAbsolutePath (path))
                leftover.original = juce::File (path);

            if (const auto ms = info->getStringAttribute (savedAtId).getLargeIntValue(); ms > 0)
                leftover.savedAt = juce::Time (ms);
        }

        return leftover;
    }

    std::unique_ptr<Project> Autosave::restore (Session& session, const Leftover& leftover, juce::String& error)
    {
        return Project::loadRecovered (session, leftover.copy, leftover.original, error);
    }

    void Autosave::discard()
    {
        if (getCopyFile().existsAsFile())
            getCopyFile().moveFileTo (getDiscardedFile());

        clear();
    }
}
