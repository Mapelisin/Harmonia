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
    }
}
