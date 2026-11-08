#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/Clips.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"
#include "ui/RecentProjects.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace clips = core::clips;

namespace
{
    struct FakeFileCommands final : FileCommands
    {
        void newProject() override          {}
        void openProject() override         {}
        void saveProject() override         {}
        void saveProjectAs() override       {}
        void exportWav() override           {}
        void exportStems() override         {}
        juce::String getProjectName() const override                    { return "Song"; }
        juce::Array<juce::File> getRecentProjects() const override      { return recent; }
        void openRecentProject (const juce::File& f) override           { opened = f; }
        void clearRecentProjects() override                             { recent.clear(); }

        juce::Array<juce::File> recent;
        juce::File opened;
    };

    const juce::PopupMenu* findSubMenu (const juce::PopupMenu& menu, const juce::String& text, bool* enabled = nullptr)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            if (it.getItem().text == text)
            {
                if (enabled != nullptr)
                    *enabled = it.getItem().isEnabled;

                return it.getItem().subMenu.get();
            }
        }

        return nullptr;
    }
}

TEST_SUITE ("recent projects and zoom to fit in the arrangement")
{
    TEST_CASE ("the list: newest first, once each, at most ten, without files that are gone")
    {
        test::TempFolder temp;
        juce::PropertiesFile::Options options;
        options.applicationName = "settings";
        options.filenameSuffix = ".xml";
        juce::PropertiesFile settings (temp.getFile ("settings.xml"), options);

        juce::Array<juce::File> files;

        for (int i = 0; i < 12; ++i)
        {
            files.add (temp.getFile ("Song " + juce::String (i) + ".harmonia"));
            files.getLast().replaceWithText ("project");
        }

        RecentProjects recent (settings);
        CHECK (recent.getFiles().isEmpty());

        recent.add (files[0]);
        recent.add (files[1]);
        recent.add (files[0]);      // again: to the top, not twice
        CHECK (recent.getFiles() == juce::Array<juce::File> { files[0], files[1] });

        for (const auto& f : files)
            recent.add (f);

        CHECK (recent.getFiles().size() == RecentProjects::maxFiles);
        CHECK (recent.getFiles().getFirst() == files[11]);
        CHECK_FALSE (recent.getFiles().contains (files[1]));

        // Kept in the settings.
        CHECK (RecentProjects (settings).getFiles() == recent.getFiles());

        // A deleted file isn't shown; remove and clear.
        files[11].deleteFile();
        CHECK (recent.getFiles().getFirst() == files[10]);
        recent.remove (files[10]);
        CHECK (recent.getFiles().getFirst() == files[9]);
        recent.clear();
        CHECK (recent.getFiles().isEmpty());
    }

    TEST_CASE ("File → Open recent lists the projects and opens the one chosen")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);

        // No list yet: the item is there but greyed out.
        bool enabled = true;
        REQUIRE (findSubMenu (main.createFileMenu(), "Open recent", &enabled) != nullptr);
        CHECK_FALSE (enabled);

        FakeFileCommands commands;
        commands.recent = { juce::File ("C:\\Music\\Songs\\Summer.harmonia"), juce::File ("D:\\Beats\\Night.harmonia") };
        main.setFileCommands (&commands);

        const auto menu = main.createFileMenu();
        const auto* recentMenu = findSubMenu (menu, "Open recent", &enabled);
        REQUIRE (recentMenu != nullptr);
        CHECK (enabled);

        juce::StringArray names;
        std::function<void()> openNight, clear;

        for (juce::PopupMenu::MenuItemIterator it (*recentMenu); it.next();)
        {
            const auto& item = it.getItem();

            if (item.isSeparator)
                continue;

            names.add (item.text);

            if (item.text == "Night")
            {
                CHECK (item.shortcutKeyDescription == "D:\\Beats");
                openNight = item.action;
            }

            if (item.text == "Clear the list")
                clear = item.action;
        }

        CHECK (names == juce::StringArray { "Summer", "Night", "Clear the list" });
        REQUIRE (openNight != nullptr);
        openNight();
        CHECK (commands.opened == juce::File ("D:\\Beats\\Night.harmonia"));

        REQUIRE (clear != nullptr);
        clear();
        CHECK (commands.recent.isEmpty());

        main.setFileCommands (nullptr);
    }

    TEST_CASE ("Z in the arrangement shows the whole song, or the selected clips; not while the keyboard plays notes")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& arrangement = main.getArrangement();
        auto& view = arrangement.getView();
        auto& model = main.getArrangementModel();
        auto& track = *project->getTracks().getFirst();

        project->addMidiClip (track, 0.0, 16.0, "Intro");
        auto* last = project->addMidiClip (track, 112.0, 16.0, "Outro");    // the song: 32 bars
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        view.pixelsPerBeat = 64.0;
        view.leftBeat = 40.0;

        CHECK (arrangement.keyPressed (juce::KeyPress ('z')));
        CHECK (view.beatToX (0.0) >= 0.0f);
        CHECK (view.beatToX (128.0) <= view.width);
        CHECK (view.beatToX (128.0) - view.beatToX (0.0) > view.width * 0.9f);

        // Just the selected clip (Я is on the Z key).
        model.deselectAll();
        model.setSelected (*last, true);
        CHECK (arrangement.keyPressed (juce::KeyPress ('Z', juce::ModifierKeys(), 0x044f)));
        CHECK (view.beatToX (112.0) >= 0.0f);
        CHECK (view.beatToX (128.0) <= view.width);
        CHECK (view.beatToX (128.0) - view.beatToX (112.0) > view.width * 0.9f);

        // The computer keyboard plays notes: Z is a note.
        const auto before = view.pixelsPerBeat;
        CHECK (main.keyPressed (juce::KeyPress ('t', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0)));
        CHECK_FALSE (arrangement.keyPressed (juce::KeyPress ('z')));
        CHECK (view.pixelsPerBeat == before);
        CHECK (main.keyPressed (juce::KeyPress ('t', juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), 0)));
    }
}
