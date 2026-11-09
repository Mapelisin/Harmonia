#include <doctest.h>

#include "TestSession.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/TrackFolders.h"
#include "ui/MainComponent.h"

using namespace harmonia;
namespace folders = core::folders;

namespace
{
    void dispatch (int ms = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    std::optional<juce::PopupMenu::Item> find (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text.startsWith (text))
                return it.getItem();

        return std::nullopt;
    }

    void choose (const juce::PopupMenu& menu, const juce::String& text)
    {
        const auto item = find (menu, text);
        REQUIRE_MESSAGE (item.has_value(), text.toStdString());
        REQUIRE_MESSAGE (item->isEnabled, text.toStdString());
        REQUIRE (item->action != nullptr);
        item->action();
    }

    /** A song of tracks A..E, each with a clip over its first bar, in the main window. */
    struct FolderFixture
    {
        FolderFixture()
        {
            project->getTracks().getFirst()->setName ("A");

            for (const auto* name : { "B", "C", "D", "E" })
                project->addTrack (name);

            for (auto* t : project->getTracks())
                project->addMidiClip (*t, 0.0, 4.0, t->getName());

            project->clearUndoHistory();
            main = std::make_unique<ui::MainComponent> (test::session(), *project);
            main->setBounds (0, 0, 1280, 800);
            dispatch();
        }

        ~FolderFixture()    { main.reset(); }

        te::AudioTrack& track (const juce::String& name)
        {
            for (auto* t : project->getTracks())
                if (t->getName() == name)
                    return *t;

            FAIL ("no track " << name.toStdString());
            return *project->getTracks().getFirst();
        }

        ui::arrangement::ArrangementModel& model()      { return main->getArrangementModel(); }
        ui::arrangement::TrackHeaders& headers()        { return main->getArrangement().getHeaders(); }

        juce::String rows()
        {
            juce::StringArray names;

            for (auto* t : model().getTracks())
                names.add (t->getName());

            return names.joinIntoString (" ");
        }

        juce::String order()
        {
            juce::StringArray names;

            for (auto* t : project->getTracks())
                names.add (t->getName());

            return names.joinIntoString (" ");
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main;
    };
}

TEST_SUITE ("ui: track folders")
{
    TEST_CASE ("the track menu makes a folder; collapsed it is one row with the folder's name, mute and solo")
    {
        FolderFixture f;
        auto& model = f.model();
        auto& headers = f.headers();
        REQUIRE (headers.getNumHeaders() == 5);
        CHECK (headers.getFoldButton (1) == nullptr);

        // A new folder of B, then C and D moved into it.
        choose (headers.createTrackMenu (f.track ("B")), "New folder with this track");
        const auto folder = model.getFolderOf (f.track ("B"));
        REQUIRE (folder.has_value());
        CHECK (folder->name == "Folder");
        CHECK (f.project->getUndoDescription() == "New folder");

        choose (headers.createFolderMenu (f.track ("C")), "Folder");     // Move into folder > Folder
        choose (headers.createFolderMenu (f.track ("D")), "Folder");
        CHECK (folders::getMembers (f.project->getEdit(), folder->id).size() == 3);
        dispatch();

        // Open: every track has its row; the first of the folder has the fold button.
        CHECK (f.rows() == "A B C D E");
        REQUIRE (headers.getNumHeaders() == 5);
        REQUIRE (headers.getFoldButton (1) != nullptr);
        CHECK (headers.getFoldButton (2) == nullptr);
        CHECK (headers.getShownName (1) == "B");

        // Collapsed: one row for the three, named after the folder.
        model.setCurrentTrack (&f.track ("C"));
        headers.getFoldButton (1)->onClick();
        dispatch();
        CHECK (f.rows() == "A B E");
        CHECK (model.getNumTracks() == 3);
        CHECK (model.getAllTracks().size() == 5);
        REQUIRE (headers.getNumHeaders() == 3);
        CHECK (headers.getShownName (1) == "Folder");
        CHECK (model.isFoldedRow (&f.track ("B")));
        CHECK_FALSE (model.isFoldedRow (&f.track ("A")));
        CHECK (model.indexOf (&f.track ("C")) == -1);
        CHECK (model.indexOf (&f.track ("E")) == 2);
        CHECK (model.getCurrentTrack() == &f.track ("B"));      // the hidden current track gave way to the row
        CHECK_FALSE (f.project->getUndoDescription().containsIgnoreCase ("collapse"));

        // Its clips are shown in that row, and can't be picked or edited there.
        int folded = 0;

        for (const auto& d : model.getClips())
            if (d.folded)
            {
                ++folded;
                CHECK (d.trackIndex == 1);
            }

        CHECK (folded == 3);
        CHECK (model.findClipAt (1, 1.0) == nullptr);
        CHECK (model.findClipAt (2, 1.0) != nullptr);
        model.selectAll();
        CHECK (model.getSelectedClips().size() == 2);
        model.deselectAll();
        CHECK (f.main->createComponentSnapshot (f.main->getLocalBounds()).isValid());

        // Mute and solo of the row: all three.
        headers.getMuteButton (1)->onClick();
        CHECK (f.track ("B").isMuted (false));
        CHECK (f.track ("C").isMuted (false));
        CHECK (f.track ("D").isMuted (false));
        CHECK_FALSE (f.track ("E").isMuted (false));
        headers.getMuteButton (1)->onClick();
        CHECK_FALSE (f.track ("C").isMuted (false));
        headers.getSoloButton (1)->onClick();
        CHECK (f.track ("D").isSolo (false));
        headers.getSoloButton (1)->onClick();
        CHECK_FALSE (f.track ("D").isSolo (false));

        // The row's menu is the folder's.
        const auto menu = headers.createTrackMenu (f.track ("B"));
        CHECK (find (menu, "Rename folder...").has_value());
        CHECK_FALSE (find (menu, "Delete track").has_value());
        choose (menu, "Show its tracks");
        dispatch();
        CHECK (f.rows() == "A B C D E");
        CHECK (headers.getShownName (1) == "B");
    }

    TEST_CASE ("moving rows: a collapsed folder moves whole; a track moved away leaves its folder, one moved in joins")
    {
        FolderFixture f;
        auto& model = f.model();
        const auto folder = folders::add (f.project->getEdit(), { &f.track ("B"), &f.track ("C") }, "Pair");
        REQUIRE (folder.has_value());
        f.project->clearUndoHistory();

        // Collapsed, its row moved to the bottom: both tracks go, still a folder.
        model.setFolderCollapsed (folder->id, true);
        CHECK (f.rows() == "A B D E");
        model.moveTrack (f.track ("B"), 3);
        CHECK (f.order() == "A D E B C");
        CHECK (f.rows() == "A D E B");
        CHECK (folders::getMembers (f.project->getEdit(), folder->id).size() == 2);
        CHECK (f.project->getUndoDescription() == "Move folder");

        // A track moved below the collapsed folder goes after all of it, not into it.
        model.moveTrack (f.track ("A"), 3);
        CHECK (f.order() == "D E B C A");
        CHECK_FALSE (model.getFolderOf (f.track ("A")).has_value());

        f.project->undo();
        f.project->undo();
        CHECK (f.order() == "A B C D E");

        // Open: E moved between B and C joins; B moved to the bottom leaves.
        model.setFolderCollapsed (folder->id, false);
        model.moveTrack (f.track ("E"), 2);
        CHECK (f.order() == "A B E C D");
        CHECK (model.getFolderOf (f.track ("E"))->id == folder->id);
        model.moveTrack (f.track ("B"), 4);
        CHECK (f.order() == "A E C D B");
        CHECK_FALSE (model.getFolderOf (f.track ("B")).has_value());
        CHECK (folders::getMembers (f.project->getEdit(), folder->id).size() == 2);

        // A deleted track's folder shrinks; the last one gone, the folder too.
        model.deleteTrack (f.track ("E"));
        model.deleteTrack (f.track ("C"));
        CHECK (folders::getAll (f.project->getEdit()).empty());
    }

    TEST_CASE ("the folder submenu: out of the folder, rename, remove")
    {
        FolderFixture f;
        auto& model = f.model();
        auto& headers = f.headers();
        const auto folder = folders::add (f.project->getEdit(), { &f.track ("B"), &f.track ("C") }, "Pair");
        REQUIRE (folder.has_value());
        dispatch();

        CHECK_FALSE (find (headers.createFolderMenu (f.track ("A")), "Take out of the folder")->isEnabled);
        choose (headers.createFolderMenu (f.track ("C")), "Take out of the folder");
        CHECK_FALSE (model.getFolderOf (f.track ("C")).has_value());
        f.project->undo();
        CHECK (model.getFolderOf (f.track ("C")).has_value());

        model.renameFolder (folder->id, "Drums");
        CHECK (model.getFolderOf (f.track ("B"))->name == "Drums");
        CHECK (find (headers.createFolderMenu (f.track ("A")), "Drums").has_value());   // to move into

        choose (headers.createFolderMenu (f.track ("B")), "Collapse to one row");
        CHECK (model.isFoldedRow (&f.track ("B")));
        choose (headers.createTrackMenu (f.track ("B")), "Remove the folder");
        dispatch();
        CHECK (folders::getAll (f.project->getEdit()).empty());
        CHECK (f.rows() == "A B C D E");
    }
}
