#include "app/MainWindow.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/Backups.h"
#include "core/Clips.h"
#include "core/SongFiles.h"
#include "core/Templates.h"
#include "ui/ExportOptions.h"
#include "ui/StemsOptions.h"
#include "ui/MainComponent.h"
#include "ui/RenderingPolicy.h"
#include "ui/Theme.h"

namespace harmonia::app
{
    namespace
    {
        constexpr int newClipBars = 4;
    }

    MainWindow::MainWindow (core::Session& s, std::unique_ptr<core::Project> p, const juce::File& projectFile)
        : juce::DocumentWindow ("Harmonia", ui::theme::colour::background, juce::DocumentWindow::allButtons),
          session (s)
    {
        // Before anything is autosaved over it.
        leftover = autosave.findLeftover();

        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (800, 500, 16384, 16384);
        setProject (std::move (p), projectFile);
        recent.add (projectFile);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        ui::applyPreferredRenderer (*this);
        startTimerHz (2);
    }

    MainWindow::~MainWindow()
    {
        stopTimer();

        // Closing normally: the work was saved or the user chose not to. Unanswered leftover
        // work stays for next time.
        if (! leftover.has_value())
            autosave.clear();

        chooser.reset();
        clearContentComponent();   // before the project it shows
        project.reset();
    }

    std::unique_ptr<core::Project> MainWindow::createNewProject (core::Session& session)
    {
        auto p = core::Project::createEmpty (session);

        // Ready to draw notes in: an instrument and an empty clip.
        if (auto* track = p->getTracks().getFirst())
        {
            track->setName ("Track 1");
            audio::instruments::useBuiltInSynth (*track);
            core::clips::getOrAddMainClip (*p, *track, newClipBars * core::clips::getBeatsPerBar (p->getEdit()));
        }

        p->clearUndoHistory();
        return p;
    }

    std::unique_ptr<core::Project> MainWindow::createStartProject (core::Session& session, juce::String* problem)
    {
        juce::String templateProblem;

        if (auto song = core::templates::createStartSong (session, templateProblem))
            return song;

        if (problem != nullptr)
            *problem = templateProblem;

        return createNewProject (session);
    }

    juce::File MainWindow::getDefaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Harmonia");
    }

    ui::MainComponent* MainWindow::getMainComponent() const
    {
        return dynamic_cast<ui::MainComponent*> (getContentComponent());
    }

    void MainWindow::setProject (std::unique_ptr<core::Project> newProject, const juce::File& newFile)
    {
        jassert (newProject != nullptr);

        // The old screen refers to the old project: it goes first.
        const auto firstTime = getContentComponent() == nullptr;
        clearContentComponent();
        project = std::move (newProject);
        file = newFile;
        autosave.projectReplaced();

        auto* content = new ui::MainComponent (session, *project);
        content->setFileCommands (this);
        setContentOwned (content, firstTime);
        updateTitle();
    }

    //==============================================================================
    void MainWindow::closeButtonPressed()
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void MainWindow::showWarning (const juce::String& text)
    {
        if (auto* content = getMainComponent())
            content->showWarning (text);
    }

    void MainWindow::showError (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (title)
                                          .withMessage (message)
                                          .withButton ("OK", 0)
                                          .withAssociatedComponent (this),
                                      nullptr);
    }

    bool MainWindow::isExporting() const
    {
        auto* content = getMainComponent();
        return content != nullptr && content->isExporting();
    }

    void MainWindow::askToSaveChanges (std::function<void()> then)
    {
        // An export has the song: it is cancelled (Esc) or ends before the song is closed.
        if (isExporting())
            return;

        if (! project->hasUnsavedChanges())
        {
            then();
            return;
        }

        if (askingToSave)
            return;

        askingToSave = true;
        juce::Component::SafePointer<MainWindow> safe (this);

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Save changes?")
                                          .withMessage ("\"" + getProjectName() + "\" has unsaved changes. Save them first?")
                                          .withButton ("Save", 1)
                                          .withButton ("Don't save", 2)
                                          .withButton ("Cancel", 0)
                                          .withAssociatedComponent (this),
                                      [safe, then = std::move (then)] (int result)
        {
            if (safe == nullptr)
                return;

            safe->askingToSave = false;

            if (result == 2)
            {
                then();
            }
            else if (result == 1)
            {
                if (safe->file != juce::File())
                {
                    if (safe->writeTo (safe->file))
                        then();
                }
                else
                {
                    safe->saveAs (then);
                }
            }
        });
    }

    //==============================================================================
    void MainWindow::newProject()
    {
        askToSaveChanges ([this]
        {
            juce::String problem;
            setProject (createStartProject (session, &problem), {});

            if (problem.isNotEmpty())
                showWarning (problem);
        });
    }

    //==============================================================================
    juce::Array<juce::File> MainWindow::getTemplates() const
    {
        return core::templates::list();
    }

    void MainWindow::newFromTemplate (const juce::File& templateFile)
    {
        askToSaveChanges ([this, templateFile]
        {
            juce::String error;
            auto song = core::templates::createSong (session, templateFile, error);

            if (song == nullptr)
            {
                showError ("Cannot open the template", error);
                return;
            }

            setProject (std::move (song), {});

            if (auto* content = getMainComponent())
                content->showMessage ("New song from the template " + core::templates::getName (templateFile));
        });
    }

    void MainWindow::saveAsTemplate()
    {
        auto* window = new juce::AlertWindow ("Save as a template",
                                              "The song as it is now (its tracks, instruments, effects, sends and clips), to start new songs from. "
                                              "The template's name:",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", file != juce::File() ? getProjectName() : juce::String ("My template"));
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
        juce::Component::SafePointer<MainWindow> safe (this);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeWindow, safe] (int result)
        {
            if (result != 1 || safeWindow == nullptr || safe == nullptr)
                return;

            const auto name = safeWindow->getTextEditorContents ("name");
            const auto target = core::templates::getFileFor (name);

            if (! target.existsAsFile())
            {
                safe->writeTemplate (name);
                return;
            }

            const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                          .withTitle ("Replace the template?")
                                                          .withMessage ("There is a template called \"" + core::templates::getName (target)
                                                                        + "\" already. Replace it with this song?")
                                                          .withButton ("Replace", 1)
                                                          .withButton ("Cancel", 0)
                                                          .withAssociatedComponent (safe.getComponent());

            juce::AlertWindow::showAsync (options, [safe, name] (int answer)
            {
                if (answer == 1 && safe != nullptr)
                    safe->writeTemplate (name);
            });
        }), true);
    }

    void MainWindow::writeTemplate (const juce::String& name)
    {
        if (const auto error = core::templates::save (*project, name); error.isNotEmpty())
        {
            showError ("Cannot save the template", error);
            return;
        }

        if (auto* content = getMainComponent())
            content->showMessage ("Template saved: " + core::templates::getName (core::templates::getFileFor (name))
                                    + " (File > Templates starts a new song from it)");
    }

    juce::File MainWindow::getStartTemplate() const
    {
        return core::templates::getStartTemplate (session);
    }

    void MainWindow::setStartTemplate (const juce::File& templateFile)
    {
        core::templates::setStartTemplate (session, templateFile);

        if (auto* content = getMainComponent())
            content->showMessage (templateFile != juce::File() ? "New songs start from the template " + core::templates::getName (templateFile)
                                                               : juce::String ("New songs start from the empty song"));
    }

    void MainWindow::showTemplatesFolder()
    {
        const auto folder = core::templates::getDefaultFolder();
        folder.createDirectory();
        folder.revealToUser();
    }

    juce::Array<juce::File> MainWindow::getRecentProjects() const
    {
        return recent.getFiles();
    }

    void MainWindow::openRecentProject (const juce::File& fileToOpen)
    {
        if (fileToOpen == file)
            return;

        askToSaveChanges ([this, fileToOpen] { open (fileToOpen); });
    }

    void MainWindow::clearRecentProjects()
    {
        recent.clear();
    }

    void MainWindow::openProject()
    {
        askToSaveChanges ([this]
        {
            const auto folder = file != juce::File() ? file.getParentDirectory() : getDefaultFolder();
            chooser = std::make_unique<juce::FileChooser> ("Open project", folder, juce::String ("*") + fileExtension);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
            {
                if (const auto result = fc.getResult(); result != juce::File())
                    open (result);
            });
        });
    }

    bool MainWindow::open (const juce::File& fileToOpen)
    {
        juce::String error;
        auto loaded = core::Project::load (session, fileToOpen, error);

        if (loaded == nullptr)
        {
            if (! fileToOpen.existsAsFile())
                recent.remove (fileToOpen);

            showError ("Cannot open the project", error);
            return false;
        }

        setProject (std::move (loaded), fileToOpen);
        recent.add (fileToOpen);

        if (auto* content = getMainComponent())
        {
            // Sounds that aren't where the song has them: said at once, not when they don't play.
            const auto missing = (int) core::song_files::getMissing (project->getEdit()).size();

            if (missing > 0)
                content->showWarning ("Opened " + fileToOpen.getFileName() + ": " + juce::String (missing)
                                        + (missing == 1 ? " sound file of it is" : " sound files of it are")
                                        + " missing (File > Find missing files)");
            else
                content->showMessage ("Opened " + fileToOpen.getFullPathName());
        }

        return true;
    }

    void MainWindow::openEarlierVersion (const juce::File& versionFile)
    {
        askToSaveChanges ([this, versionFile]
        {
            for (const auto& version : core::backups::list (file))
            {
                if (version.file != versionFile)
                    continue;

                juce::String error;
                auto earlier = core::backups::open (session, version, file, error);

                if (earlier == nullptr)
                {
                    showError ("Cannot open the earlier version", error);
                    return;
                }

                const auto songFile = file;
                setProject (std::move (earlier), songFile);

                if (auto* content = getMainComponent())
                    content->showMessage ("The song as it was on " + version.getLabel()
                                            + ". Ctrl+S makes it the song again (what the song is now is kept as an earlier version)");

                return;
            }

            showError ("Cannot open the earlier version", "It isn't there any more: " + versionFile.getFullPathName());
        });
    }

    void MainWindow::offerToRestore (std::function<void()> otherwise)
    {
        if (! leftover.has_value())
        {
            if (otherwise != nullptr)
                otherwise();

            return;
        }

        const auto when = leftover->savedAt.toString (true, true, false, true);
        juce::Component::SafePointer<MainWindow> safe (this);

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Restore unsaved work?")
                                          .withMessage ("Harmonia didn't close properly last time. Unsaved changes to \""
                                                        + leftover->getName() + "\" were kept (" + when + ").\n\n"
                                                        + "Restore them?")
                                          .withButton ("Restore", 1)
                                          .withButton ("Don't restore", 0)
                                          .withAssociatedComponent (this),
                                      [safe, otherwise = std::move (otherwise)] (int result)
        {
            if (safe == nullptr || ! safe->leftover.has_value())
                return;

            const auto found = *std::exchange (safe->leftover, std::nullopt);

            if (result == 1 && safe->restore (found))
                return;

            if (result != 1)
                safe->autosave.discard();

            if (otherwise != nullptr)
                otherwise();
        });
    }

    bool MainWindow::restore (const core::Autosave::Leftover& found)
    {
        juce::String error;
        auto restored = core::Autosave::restore (session, found, error);

        if (restored == nullptr)
        {
            autosave.discard();
            showError ("Cannot restore the unsaved work",
                       error + "\n\nThe copy is kept as " + autosave.getDiscardedFile().getFullPathName());
            return false;
        }

        setProject (std::move (restored), found.original);

        if (auto* content = getMainComponent())
            content->showMessage ("Restored the unsaved work on " + found.getName() + ". Save it with Ctrl+S");

        return true;
    }

    void MainWindow::saveProject()
    {
        if (file == juce::File())
            saveAs ({});
        else
            writeTo (file);
    }

    void MainWindow::saveProjectAs()
    {
        saveAs ({});
    }

    void MainWindow::saveAs (std::function<void()> then)
    {
        const auto folder = file != juce::File() ? file.getParentDirectory() : getDefaultFolder();
        folder.createDirectory();

        chooser = std::make_unique<juce::FileChooser> ("Save project", folder.getChildFile (getProjectName() + fileExtension),
                                                       juce::String ("*") + fileExtension);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, then = std::move (then)] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();

            if (result == juce::File())
                return;

            result = result.withFileExtension (fileExtension);

            if (writeTo (result))
            {
                file = result;
                updateTitle();

                if (then != nullptr)
                    then();
            }
        });
    }

    bool MainWindow::writeTo (const juce::File& target)
    {
        // What the file had goes to the song's earlier versions first.
        juce::String versionProblem;

        if (const auto error = core::backups::save (*project, target, versionProblem); error.isNotEmpty())
        {
            showError ("Cannot save the project", error);
            return false;
        }

        recent.add (target);
        updateTitle();

        if (auto* content = getMainComponent())
        {
            if (versionProblem.isNotEmpty())
                content->showWarning ("Saved " + target.getFullPathName() + ", but the earlier version couldn't be kept: " + versionProblem);
            else
                content->showMessage ("Saved " + target.getFullPathName());
        }

        return true;
    }

    void MainWindow::exportSong()
    {
        ui::ExportOptions::show (session, [safe = juce::Component::SafePointer<MainWindow> (this)] (const audio::ExportFormat& format)
        {
            if (safe != nullptr)
                safe->chooseExport (safe->getProjectName(), std::nullopt, format);
        });
    }

    void MainWindow::exportLoop()
    {
        auto* content = getMainComponent();
        const auto region = content != nullptr ? content->getArrangementModel().getLoopRegion() : std::nullopt;

        if (! region.has_value() || region->getLength() <= 0.0)
        {
            showError ("Nothing to export", "Set a loop region first: Ctrl+drag along the ruler above the arrangement's tracks (or a marker's menu: Loop this section).");
            return;
        }

        auto& tempo = project->getEdit().tempoSequence;
        const te::TimeRange part (tempo.toTime (te::BeatPosition::fromBeats (region->start)),
                                  tempo.toTime (te::BeatPosition::fromBeats (region->end)));
        const auto name = getProjectName() + " - " + ui::MainComponent::describeBars (*region, content->getArrangementModel().getBeatsPerBar());

        // A loop is a sample for other songs: a WAV at full quality unless chosen otherwise (it
        // remembers its own format, not the song's).
        ui::ExportOptions::show (session, [safe = juce::Component::SafePointer<MainWindow> (this), name, part] (const audio::ExportFormat& format)
        {
            if (safe != nullptr)
                safe->chooseExport (name, part, format);
        }, audio::ExportUse::loop);
    }

    void MainWindow::chooseExport (const juce::String& defaultName, std::optional<te::TimeRange> part, const audio::ExportFormat& format)
    {
        const auto folder = file != juce::File() ? file.getParentDirectory() : getDefaultFolder();
        folder.createDirectory();

        const auto extension = format.getExtension();
        chooser = std::make_unique<juce::FileChooser> ("Export " + format.describe(),
                                                       folder.getChildFile (juce::File::createLegalFileName (defaultName + extension)),
                                                       "*" + extension);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, part, format] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();

            if (result == juce::File())
                return;

            result = result.withFileExtension (format.getExtension());
            auto* content = getMainComponent();

            if (content == nullptr)
                return;

            // Rendering takes over the engine: stop first, or playback would resume over it.
            content->getTransport().stop();

            // A bit at a time, with its progress over the window: it can be cancelled.
            content->runExport (audio::ExportJob::song (project->getEdit(), result, format, part), [this] (audio::ExportJob& job)
            {
                if (job.getError().isNotEmpty())
                    showError ("Export failed", job.getError());
                else if (auto* c = getMainComponent())
                    c->showMessage ("Exported " + job.getFiles().getFirst().getFullPathName());
            });
        });
    }

    void MainWindow::exportStems()
    {
        // Which tracks, what the files are called and their format first; then where they go.
        ui::StemsOptions::show (session, project->getEdit(), getProjectName(),
                                [safe = juce::Component::SafePointer<MainWindow> (this)] (const audio::StemsOptions& options)
                                {
                                    if (safe != nullptr)
                                        safe->exportStemsInto (options);
                                },
                                [safe = juce::Component::SafePointer<MainWindow> (this)] (const juce::String& reason)
                                {
                                    if (safe != nullptr)
                                        safe->showError ("Export stems", reason);
                                });
    }

    void MainWindow::exportStemsInto (const audio::StemsOptions& options)
    {
        const auto folder = file != juce::File() ? file.getParentDirectory() : getDefaultFolder();
        folder.createDirectory();

        chooser = std::make_unique<juce::FileChooser> ("Export stems into folder", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, options] (const juce::FileChooser& fc)
        {
            const auto target = fc.getResult();

            if (target == juce::File())
                return;

            auto* content = getMainComponent();

            if (content == nullptr)
                return;

            content->getTransport().stop();

            // A stem after another, with the progress over the window: it can be cancelled.
            content->runExport (audio::ExportJob::stems (project->getEdit(), target, options), [this, target] (audio::ExportJob& job)
            {
                if (job.getError().isNotEmpty())
                {
                    showError ("Export failed", job.getError());
                    return;
                }

                if (auto* c = getMainComponent())
                    c->showMessage ("Exported " + juce::String (job.getFiles().size()) + " stems to " + target.getFullPathName());

                job.getFiles().getFirst().revealToUser();
            });
        });
    }

    juce::String MainWindow::getProjectName() const
    {
        return file != juce::File() ? file.getFileNameWithoutExtension() : juce::String ("Untitled");
    }

    //==============================================================================
    void MainWindow::updateTitle()
    {
        const auto title = getProjectName() + (project->hasUnsavedChanges() ? " *" : "")
                         + juce::String::fromUTF8 (" \xe2\x80\x94 Harmonia");

        if (getName() != title)
            setName (title);
    }

    void MainWindow::timerCallback()
    {
        updateTitle();

        if (leftover.has_value())
            return;

        // While the song is exported it is not as the user left it (tracks silenced for a stem):
        // no copy of it is made then.
        if (isExporting())
            return;

        // A problem is shown once, not twice a second; again if it changes.
        if (const auto problem = autosave.update (*project, file); problem != autosaveProblem)
        {
            autosaveProblem = problem;

            if (problem.isNotEmpty())
                showWarning ("Autosave: " + problem);
        }
    }
}
