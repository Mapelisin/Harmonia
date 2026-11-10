#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "core/Backups.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/SongFiles.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace backups = core::backups;
namespace song_files = core::song_files;

namespace
{
    void dispatch (int ms = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** A tone in a file, in a folder that may not be there yet. */
    juce::File tone (const juce::File& file, double seconds, float level = 0.5f)
    {
        file.getParentDirectory().createDirectory();
        return test::writeTone (file, seconds, level);
    }

    /** The window's side of the File menu: where the song is saved, and which version was asked for. */
    struct FakeFileCommands final : FileCommands
    {
        void newProject() override          {}
        void openProject() override         {}
        void saveProject() override         {}
        void saveProjectAs() override       {}
        void exportSong() override          {}
        void exportStems() override         {}
        juce::String getProjectName() const override                    { return projectFile.getFileNameWithoutExtension(); }
        juce::File getProjectFile() const override                      { return projectFile; }
        void openEarlierVersion (const juce::File& f) override          { openedVersion = f; }

        juce::File projectFile, openedVersion;
    };

    std::optional<juce::PopupMenu::Item> find (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, false); it.next();)
            if (it.getItem().text.startsWith (text))
                return it.getItem();

        return std::nullopt;
    }

    juce::StringArray itemsOf (const juce::PopupMenu& menu)
    {
        juce::StringArray result;

        for (juce::PopupMenu::MenuItemIterator it (menu, false); it.next();)
            if (! it.getItem().isSeparator)
                result.add (it.getItem().text);

        return result;
    }
}

TEST_SUITE ("ui: the song's files and earlier versions")
{
    TEST_CASE ("File > Earlier versions lists what the song was before the last saves; a click asks for that one")
    {
        test::TempFolder temp;
        const auto file = temp.getFile ("Song.harmonia");
        auto project = core::Project::createEmpty (test::session());
        auto main = std::make_unique<MainComponent> (test::session(), *project);
        FakeFileCommands commands;
        main->setFileCommands (&commands);

        // Never saved: nothing to go back to.
        auto menu = main->createFileMenu();
        REQUIRE (find (menu, "Earlier versions").has_value());
        CHECK_FALSE (find (menu, "Earlier versions")->isEnabled);

        // Saved three times, changed in between: two earlier versions.
        juce::String warning;
        commands.projectFile = file;
        REQUIRE (backups::save (*project, file, warning).isEmpty());
        project->addTrack ("Bass");
        REQUIRE (backups::save (*project, file, warning).isEmpty());
        project->addTrack ("Lead");
        REQUIRE (backups::save (*project, file, warning).isEmpty());
        const auto versions = backups::list (file);
        REQUIRE (versions.size() == 2);

        menu = main->createFileMenu();
        const auto item = find (menu, "Earlier versions");
        REQUIRE (item.has_value());
        CHECK (item->isEnabled);
        REQUIRE (item->subMenu != nullptr);
        CHECK (itemsOf (*item->subMenu) == juce::StringArray { versions[0].getLabel(), versions[1].getLabel(), "Show their folder" });

        find (*item->subMenu, versions[1].getLabel())->action();
        CHECK (commands.openedVersion == versions[1].file);

        main.reset();
    }

    TEST_CASE ("File > Collect all files copies the song's sounds next to it; File > Find missing files finds moved ones")
    {
        test::TempFolder temp;
        const auto file = temp.getFile ("Songs").getChildFile ("Song.harmonia");
        const auto sound = tone (temp.getFile ("Elsewhere").getChildFile ("Guitar.wav"), 1.0);
        juce::String error;

        auto project = core::Project::createEmpty (test::session());
        auto* track = project->getTracks().getFirst();
        track->setName ("Guitars");
        core::tracks::setKind (*track, core::tracks::Kind::audio);
        auto* clip = project->addAudioClip (*track, sound, 0.0, error);
        REQUIRE_MESSAGE (clip != nullptr, error.toStdString());
        clip->setName ("Riff");
        dispatch();

        auto main = std::make_unique<MainComponent> (test::session(), *project);
        main->setBounds (0, 0, 1280, 800);
        FakeFileCommands commands;
        main->setFileCommands (&commands);
        dispatch();
        project->clearUndoHistory();

        // A song that was never saved has no folder yet: said, nothing done.
        auto menu = main->createFileMenu();
        REQUIRE (find (menu, "Collect all files into the song's folder").has_value());
        find (menu, "Collect all files into the song's folder")->action();
        CHECK (main->getStatusBar().getWarning().startsWith ("Save the song first"));
        CHECK_FALSE (project->canUndo());

        REQUIRE (project->save (file).isEmpty());
        commands.projectFile = file;
        main->getStatusBar().setWarning ({});

        // Nothing is missing: that item is greyed out and says so.
        menu = main->createFileMenu();
        REQUIRE (find (menu, "Find missing files (none are missing)").has_value());
        CHECK_FALSE (find (menu, "Find missing files")->isEnabled);

        find (menu, "Collect all files into the song's folder")->action();
        const auto copy = song_files::getFolder (file).getChildFile ("Guitar.wav");
        CHECK (copy.hasIdenticalContentTo (sound));
        CHECK (clip->getSourceFileReference().getFile() == copy);
        CHECK (project->getUndoDescription() == "Collect files");
        CHECK (main->getStatusBar().getMessage().startsWith ("Copied 1 file ("));
        CHECK (main->getStatusBar().getMessage().endsWith ("Save the song (Ctrl+S) to keep it so; Ctrl+Z takes it back"));
        CHECK (main->getStatusBar().getWarning().isEmpty());

        // Again: said, nothing to undo twice.
        main->collectFiles();
        CHECK (main->getStatusBar().getMessage().startsWith ("All the song's sound files are in "));

        // The copy is moved away (the folder was tidied up by hand): the menu counts it, the
        // question names it, and looking in the right folder finds it.
        const auto newPlace = temp.getFile ("Tidy").getChildFile ("Guitars");
        REQUIRE (newPlace.createDirectory());
        test::session().getEngine().getAudioFileManager().releaseAllFiles();
        REQUIRE (copy.moveFileTo (newPlace.getChildFile ("Guitar.wav")));

        menu = main->createFileMenu();
        REQUIRE (find (menu, "Find missing files (1)...").has_value());
        CHECK (find (menu, "Find missing files (1)...")->isEnabled);
        CHECK (main->describeMissingFiles() == "Guitar.wav  (was in " + song_files::getFolder (file).getFullPathName() + "; used by Riff on Guitars)");

        main->lookForMissingFilesIn (temp.getFile ("Elsewhere").getChildFile ("Nothing here"));
        CHECK (main->getStatusBar().getWarning().startsWith ("None of the missing files is in "));

        main->lookForMissingFilesIn (temp.getFile ("Tidy"));
        CHECK (clip->getSourceFileReference().getFile() == newPlace.getChildFile ("Guitar.wav"));
        CHECK (main->getStatusBar().getMessage().startsWith ("Found the missing file"));
        CHECK (main->getStatusBar().getWarning().isEmpty());
        CHECK (project->getUndoDescription() == "Find missing files");
        CHECK (find (main->createFileMenu(), "Find missing files (none are missing)").has_value());

        project->undo();
        dispatch();
        CHECK (find (main->createFileMenu(), "Find missing files (1)...").has_value());

        main.reset();
    }
}
