#pragma once

#include <juce_core/juce_core.h>

namespace harmonia::ui
{
    /** Project file commands, implemented by the main window (which owns the project and can
        replace it). Each may ask the user (file dialogs, "save changes?") and finishes later. */
    struct FileCommands
    {
        virtual ~FileCommands() = default;

        virtual void newProject() = 0;      // Ctrl+N
        virtual void openProject() = 0;     // Ctrl+O
        virtual void saveProject() = 0;     // Ctrl+S
        virtual void saveProjectAs() = 0;   // Ctrl+Shift+S
        virtual void exportSong() = 0;      // Ctrl+R, as in FL Studio: asks the format, then where
        virtual void exportStems() = 0;     // a file per track or group: asks which, then where

        /** The loop region set on the arrangement's ruler, exactly, as a WAV (to use as a sample). */
        virtual void exportLoopWav()                                    {}

        /** The project's file name without extension, or "Untitled". */
        virtual juce::String getProjectName() const = 0;

        /** Where the open song is saved (none: it never was). */
        virtual juce::File getProjectFile() const                       { return {}; }

        /** Opens an earlier version of the open song (one of core::backups::list) in its place,
            unsaved. */
        virtual void openEarlierVersion (const juce::File&)             {}

        /** Projects opened or saved lately, newest first (File → Open recent). */
        virtual juce::Array<juce::File> getRecentProjects() const      { return {}; }
        virtual void openRecentProject (const juce::File&)              {}
        virtual void clearRecentProjects()                              {}
    };
}
