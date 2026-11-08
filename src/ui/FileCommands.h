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
        virtual void exportWav() = 0;       // Ctrl+R, as in FL Studio
        virtual void exportStems() = 0;     // a WAV per track

        /** The project's file name without extension, or "Untitled". */
        virtual juce::String getProjectName() const = 0;
    };
}
