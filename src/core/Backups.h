#pragma once

#include "core/Project.h"

/** Earlier versions of a song: every time a save is about to replace the project file, what is in
    it is kept in a folder next to it ("<song> Backups"), named by when it was saved. The last
    maxVersions are kept. An earlier version opens as the song itself, unsaved: saving it keeps
    the version it replaces too, so nothing is ever lost by looking back. */
namespace harmonia::core::backups
{
    inline constexpr int maxVersions = 20;

    /** Where the earlier versions of the project at `projectFile` are: "<name> Backups" beside it
        (no file: none). */
    juce::File getFolder (const juce::File& projectFile);

    struct Version
    {
        juce::File file;
        juce::Time savedAt;

        /** "10 Nov 2026 14:03:22" */
        juce::String getLabel() const;
    };

    /** The earlier versions kept, the latest first. */
    std::vector<Version> list (const juce::File& projectFile);

    /** Keeps what is in the project file now as an earlier version (nothing to keep if there is
        no file yet, or if the latest version kept is the same already), then drops the oldest
        beyond maxVersions. Returns what went wrong, empty if nothing did. */
    juce::String keep (const juce::File& projectFile);

    /** Saves the project to `file`, keeping what the file had as an earlier version first.
        Returns the save's error (empty: saved). A version that couldn't be kept doesn't stop the
        save: `warning` says so. */
    juce::String save (Project&, const juce::File& file, juce::String& warning);

    /** Opens an earlier version as the project at `projectFile`, with unsaved changes: Ctrl+S
        puts it in the song's place (and keeps what was there). */
    std::unique_ptr<Project> open (Session&, const Version&, const juce::File& projectFile, juce::String& error);
}
