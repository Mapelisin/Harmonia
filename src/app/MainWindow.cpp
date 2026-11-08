#include "app/MainWindow.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
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
            showError ("Cannot open the project", error);
            return false;
        }

        setProject (std::move (loaded), fileToOpen);

        if (auto* content = getMainComponent())
            content->showMessage ("Opened " + fileToOpen.getFullPathName());

        return true;
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
        if (const auto error = project->save (target); error.isNotEmpty())
        {
            showError ("Cannot save the project", error);
            return false;
        }

        updateTitle();

        if (auto* content = getMainComponent())
            content->showMessage ("Saved " + target.getFullPathName());

        return true;
    }

    void MainWindow::exportWav()
    {
        const auto folder = file != juce::File() ? file.getParentDirectory() : getDefaultFolder();
        folder.createDirectory();

        chooser = std::make_unique<juce::FileChooser> ("Export WAV", folder.getChildFile (getProjectName() + ".wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();

            if (result == juce::File())
                return;

            result = result.withFileExtension (".wav");

            // Rendering takes over the engine: stop first, or playback would resume over it.
            if (auto* content = getMainComponent())
                content->getTransport().stop();

            if (const auto error = audio::exportWav (project->getEdit(), result); error.isNotEmpty())
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
        {
            setName (title);

            if (auto* content = getMainComponent())
                content->repaint (content->getLocalBounds().removeFromTop (ui::theme::size::headerHeight));
        }
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
