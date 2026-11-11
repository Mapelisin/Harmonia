#pragma once

#include "audio/Export.h"
#include "audio/ExportJob.h"
#include "core/Autosave.h"
#include "core/Project.h"
#include "core/ProjectFiles.h"
#include "ui/FileCommands.h"
#include "ui/RecentProjects.h"

namespace harmonia::ui { class MainComponent; }

namespace harmonia::app
{
    /** The main window. Owns the open project and replaces it on New / Open; saves, exports
        and asks about unsaved changes. The title shows the project name and * when changed.
        Unsaved work is copied every half minute (core::Autosave) and offered back after a crash. */
    class MainWindow final : public juce::DocumentWindow,
                             public ui::FileCommands,
                             private juce::Timer
    {
    public:
        MainWindow (core::Session&, std::unique_ptr<core::Project>, const juce::File& projectFile = {});
        ~MainWindow() override;

        void closeButtonPressed() override;

        /** Shows a problem in the status bar. */
        void showWarning (const juce::String&);

        /** If the project has unsaved changes, asks whether to save them first; then calls
            `then`, unless the user cancelled (or saving failed). */
        void askToSaveChanges (std::function<void()> then);

        core::Project& getProject() noexcept        { return *project; }

        /** A new project: one track with the built-in synth and an empty 4-bar clip. */
        static std::unique_ptr<core::Project> createNewProject (core::Session&);

        /** What a new song is: one from the template new songs start from (core::templates), or
            createNewProject if there is none or it can't be opened (`problem` then says why). */
        static std::unique_ptr<core::Project> createStartProject (core::Session&, juce::String* problem = nullptr);

        /** Where projects and exports go by default: Documents/Harmonia. */
        static juce::File getDefaultFolder();

        static constexpr const char* fileExtension = core::project_files::extension;

        // ui::FileCommands
        void newProject() override;
        void openProject() override;
        void saveProject() override;
        void saveProjectAs() override;
        void exportSong() override;
        void exportStems() override;
        void exportStemsInto (const audio::StemsOptions&);
        void exportLoop() override;
        juce::String getProjectName() const override;
        juce::File getProjectFile() const override                      { return file; }
        void openEarlierVersion (const juce::File&) override;
        juce::Array<juce::File> getRecentProjects() const override;
        void openRecentProject (const juce::File&) override;
        void clearRecentProjects() override;
        juce::Array<juce::File> getTemplates() const override;
        void newFromTemplate (const juce::File&) override;
        void saveAsTemplate() override;
        juce::File getStartTemplate() const override;
        void setStartTemplate (const juce::File&) override;
        void showTemplatesFolder() override;

        /** Opens a project file (also used for a file given on the command line). */
        bool open (const juce::File&);

        /** If Harmonia didn't close normally last time and left unsaved work, asks whether to
            restore it. Calls `otherwise` when there was none or it wasn't restored. */
        void offerToRestore (std::function<void()> otherwise);

    private:
        core::Session& session;
        std::unique_ptr<core::Project> project;
        juce::File file;
        std::unique_ptr<juce::FileChooser> chooser;
        bool askingToSave = false;

        core::Autosave autosave;
        ui::RecentProjects recent { session.getEngine().getPropertyStorage().getPropertiesFile() };
        std::optional<core::Autosave::Leftover> leftover;   // until the user says what to do with it
        juce::String autosaveProblem;

        ui::MainComponent* getMainComponent() const;
        void setProject (std::unique_ptr<core::Project>, const juce::File&);
        void saveAs (std::function<void()> then);
        bool writeTo (const juce::File&);
        void writeTemplate (const juce::String& name);
        void showError (const juce::String& title, const juce::String& message);

        /** Asks where to save `defaultName` in `format`, then exports the song (or `part` of it) there. */
        void chooseExport (const juce::String& defaultName, std::optional<te::TimeRange> part, const audio::ExportFormat& format);

        /** The song is being exported: it is not to be replaced, closed or copied meanwhile. */
        bool isExporting() const;
        void updateTitle();
        bool restore (const core::Autosave::Leftover&);
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (MainWindow)
    };
}
