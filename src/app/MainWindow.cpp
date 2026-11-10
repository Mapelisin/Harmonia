#include "app/MainWindow.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/Backups.h"
#include "core/Clips.h"
#include "core/SongFiles.h"
#include "ui/ExportOptions.h"
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

    void MainWindow::askToSaveChanges (std::function<void()> then)
    {
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
        askToSaveChanges ([this] { setProject (createNewProject (session), {}); });
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

    void MainWindow::exportLoopWav()
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
        // A loop is a sample for other songs: a WAV at full quality.
        chooseExport (getProjectName() + " - " + ui::MainComponent::describeBars (*region, content->getArrangementModel().getBeatsPerBar()),
                      part, {});
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

            // Rendering takes over the engine: stop first, or playback would resume over it.
            if (auto* content = getMainComponent())
                content->getTransport().stop();

            // Rendering holds the window up for a while (an MP3 is encoded after it).
            juce::MouseCursor::showWaitCursor();
            const auto error = audio::exportSong (project->getEdit(), result, format, part);
            juce::MouseCursor::hideWaitCursor();

            if (error.isNotEmpty())
                showError ("Export failed", error);
            else if (auto* content = getMainComponent())
                content->showMessage ("Exported " + result.getFullPathName());
        });
    }

    void MainWindow::exportStems()
    {
        const auto folder = file != juce::File() ? file.getParentDirectory() : getDefaultFolder();
        folder.createDirectory();

        chooser = std::make_unique<juce::FileChooser> ("Export stems into folder", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            const auto target = fc.getResult();

            if (target == juce::File())
                return;

            auto* content = getMainComponent();

            if (content != nullptr)
                content->getTransport().stop();

            // Rendering holds the window up: say how far it got.
            juce::MouseCursor::showWaitCursor();

            const auto result = audio::exportStems (project->getEdit(), target, getProjectName(), [this] (int done, int total)
            {
                if (done < total)
                    if (auto* c = getMainComponent())
                    {
                        c->showMessage ("Exporting stem " + juce::String (done + 1) + " of " + juce::String (total) + "...");

                        if (auto* peer = getPeer())
                            peer->performAnyPendingRepaintsNow();
                    }
            });

            juce::MouseCursor::hideWaitCursor();

            if (result.error.isNotEmpty())
            {
                showError ("Export failed", result.error);
                return;
            }

            if (auto* c = getMainComponent())
                c->showMessage ("Exported " + juce::String (result.files.size()) + " stems to " + target.getFullPathName());

            result.files.getFirst().revealToUser();
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

        // A problem is shown once, not twice a second; again if it changes.
        if (const auto problem = autosave.update (*project, file); problem != autosaveProblem)
        {
            autosaveProblem = problem;

            if (problem.isNotEmpty())
                showWarning ("Autosave: " + problem);
        }
    }
}
