#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/Templates.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace templates = core::templates;

namespace
{
    /** The window's template commands over a folder of the test's own. */
    struct FakeFileCommands final : FileCommands
    {
        explicit FakeFileCommands (const juce::File& f) : folder (f) {}

        void newProject() override          {}
        void openProject() override         {}
        void saveProject() override         {}
        void saveProjectAs() override       {}
        void exportSong() override          {}
        void exportStems() override         {}
        juce::String getProjectName() const override    { return "Song"; }

        juce::Array<juce::File> getTemplates() const override          { return templates::list (folder); }
        void newFromTemplate (const juce::File& file) override          { opened.add (file); }
        void saveAsTemplate() override                                  { ++saves; }
        juce::File getStartTemplate() const override                    { return start; }
        void setStartTemplate (const juce::File& file) override         { start = file; }
        void showTemplatesFolder() override                             { ++shown; }

        juce::File folder, start;
        juce::Array<juce::File> opened;
        int saves = 0, shown = 0;
    };

    /** The item of a menu whose text starts so (a copy: the menu may be a temporary). */
    std::optional<juce::PopupMenu::Item> findItem (const juce::PopupMenu& menu, const juce::String& start)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (const auto& item = it.getItem(); item.text.startsWith (start) && ! item.isSectionHeader)
                return item;

        return std::nullopt;
    }

    juce::StringArray textsOf (const juce::PopupMenu& menu)
    {
        juce::StringArray texts;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (const auto& item = it.getItem(); ! item.isSeparator)
                texts.add (item.text);

        return texts;
    }
}

TEST_SUITE ("ui: the Templates menu")
{
    TEST_CASE ("File > Templates: a new song from one, this song saved as one, the one new songs start from")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        // Without the window's commands there is nothing to open.
        auto item = findItem (main.createFileMenu(), "Templates");
        REQUIRE (item.has_value());
        CHECK_FALSE (item->isEnabled);

        FakeFileCommands commands (temp.getFile ("Templates"));
        main.setFileCommands (&commands);

        // No templates yet: how to get one.
        item = findItem (main.createFileMenu(), "Templates");
        REQUIRE (item.has_value());
        CHECK (item->isEnabled);
        REQUIRE (item->subMenu != nullptr);
        CHECK (textsOf (*item->subMenu) == juce::StringArray ({ "New song from", "No templates yet: save a song as one",
                                                               "Save this song as a template...",
                                                               "New songs start from: the empty song", "Show the templates folder" }));
        CHECK_FALSE (findItem (*item->subMenu, "No templates yet")->isEnabled);

        auto save = findItem (*item->subMenu, "Save this song as a template");
        REQUIRE (save.has_value());
        REQUIRE (save->action != nullptr);
        save->action();
        CHECK (commands.saves == 1);

        auto show = findItem (*item->subMenu, "Show the templates folder");
        REQUIRE (show.has_value());
        show->action();
        CHECK (commands.shown == 1);

        // Two templates: each starts a new song.
        REQUIRE (templates::save (*project, "Band", commands.folder).isEmpty());
        REQUIRE (templates::save (*project, "Beat", commands.folder).isEmpty());
        const auto band = templates::getFileFor ("Band", commands.folder), beat = templates::getFileFor ("Beat", commands.folder);

        const auto menu = main.createTemplatesMenu();
        CHECK (textsOf (menu) == juce::StringArray ({ "New song from", "Band", "Beat", "Save this song as a template...",
                                                     "New songs start from: the empty song", "Show the templates folder" }));

        auto open = findItem (menu, "Beat");
        REQUIRE (open.has_value());
        CHECK_FALSE (open->isTicked);
        REQUIRE (open->action != nullptr);
        open->action();
        REQUIRE (commands.opened.size() == 1);
        CHECK (commands.opened.getFirst() == beat);

        // What new songs start from: the empty song at first, then the one chosen.
        auto start = findItem (menu, "New songs start from");
        REQUIRE (start.has_value());
        REQUIRE (start->subMenu != nullptr);
        CHECK (textsOf (*start->subMenu) == juce::StringArray ({ "The empty song", "Band", "Beat" }));
        CHECK (findItem (*start->subMenu, "The empty song")->isTicked);
        CHECK_FALSE (findItem (*start->subMenu, "Band")->isTicked);

        findItem (*start->subMenu, "Band")->action();
        CHECK (commands.start == band);
        CHECK (commands.opened.size() == 1);        // choosing it opens nothing

        start = findItem (main.createTemplatesMenu(), "New songs start from");
        REQUIRE (start.has_value());
        CHECK (start->text == "New songs start from: Band");
        CHECK (findItem (*start->subMenu, "Band")->isTicked);
        CHECK_FALSE (findItem (*start->subMenu, "The empty song")->isTicked);
        CHECK_FALSE (findItem (*start->subMenu, "Beat")->isTicked);

        findItem (*start->subMenu, "The empty song")->action();
        CHECK (commands.start == juce::File());

        main.setFileCommands (nullptr);
    }
}
