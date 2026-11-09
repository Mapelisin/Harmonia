#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "core/Project.h"
#include "core/TrackFolders.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace folders = core::folders;

namespace
{
    /** A song of tracks A..F. */
    struct SixTracks
    {
        SixTracks()
        {
            project->getTracks().getFirst()->setName ("A");

            for (const auto* name : { "B", "C", "D", "E", "F" })
                project->addTrack (name);

            project->clearUndoHistory();
        }

        te::AudioTrack& track (const juce::String& name)
        {
            for (auto* t : project->getTracks())
                if (t->getName() == name)
                    return *t;

            FAIL ("no track " << name.toStdString());
            return *project->getTracks().getFirst();
        }

        /** "A B [C D] E F": the tracks in order, a folder's in brackets. */
        juce::String layout()
        {
            juce::String text, open;

            for (auto* t : project->getTracks())
            {
                const auto folder = folders::getFolderOf (*t);
                const auto id = folder.has_value() ? folder->id : juce::String();

                if (id != open)
                {
                    if (open.isNotEmpty())
                        text = text.trimEnd() + "] ";

                    if (id.isNotEmpty())
                        text << "[";

                    open = id;
                }

                text << t->getName() << " ";
            }

            return text.trimEnd() + (open.isNotEmpty() ? "]" : "");
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::Edit& edit = project->getEdit();
    };
}

TEST_SUITE ("core::track folders")
{
    TEST_CASE ("a folder brings its tracks together where the first is; renamed, removed, one undo step each")
    {
        SixTracks song;
        CHECK (folders::getAll (song.edit).empty());
        CHECK (folders::getNewName (song.edit) == "Folder");

        song.project->beginGesture ("New folder");
        const auto drums = folders::add (song.edit, { &song.track ("E"), &song.track ("B"), &song.track ("C") }, "Drums");
        REQUIRE (drums.has_value());
        CHECK (drums->name == "Drums");
        CHECK_FALSE (drums->collapsed);
        CHECK (song.layout() == "A [B C E] D F");
        CHECK (folders::getMembers (song.edit, drums->id).size() == 3);
        CHECK (folders::getFolderOf (song.track ("C"))->id == drums->id);
        CHECK_FALSE (folders::getFolderOf (song.track ("D")).has_value());
        CHECK (folders::getNewName (song.edit) == "Folder");

        song.project->undo();
        CHECK (song.layout() == "A B C D E F");
        CHECK (folders::getAll (song.edit).empty());
        song.project->redo();
        CHECK (song.layout() == "A [B C E] D F");

        // A second one; listed in song order.
        song.project->beginGesture ("New folder");
        const auto first = folders::add (song.edit, { &song.track ("A") }, {});
        REQUIRE (first.has_value());
        CHECK (first->name == "Folder");
        CHECK (folders::getNewName (song.edit) == "Folder 2");
        CHECK (song.layout() == "[A] [B C E] D F");
        REQUIRE (folders::getAll (song.edit).size() == 2);
        CHECK (folders::getAll (song.edit)[0].id == first->id);

        song.project->beginGesture ("Rename folder");
        folders::rename (song.edit, drums->id, "  Kit ");
        CHECK (folders::getFolderOf (song.track ("B"))->name == "Kit");
        folders::rename (song.edit, drums->id, " ");
        CHECK (folders::getFolderOf (song.track ("B"))->name == "Kit");

        song.project->beginGesture ("Remove folder");
        folders::remove (song.edit, drums->id);
        CHECK (song.layout() == "[A] B C E D F");
        song.project->undo();
        CHECK (song.layout() == "[A] [B C E] D F");

        CHECK_FALSE (folders::add (song.edit, {}, "Empty").has_value());
    }

    TEST_CASE ("a track joins a folder after its last track, and leaves below it")
    {
        SixTracks song;
        const auto folder = folders::add (song.edit, { &song.track ("B"), &song.track ("C") }, "Pair");
        REQUIRE (folder.has_value());
        CHECK (song.layout() == "A [B C] D E F");

        folders::setFolder (song.track ("F"), folder->id);
        CHECK (song.layout() == "A [B C F] D E");
        folders::setFolder (song.track ("A"), folder->id);
        CHECK (song.layout() == "[B C F A] D E");

        folders::setFolder (song.track ("C"), {});
        CHECK (song.layout() == "[B F A] C D E");

        // From one folder to another.
        const auto other = folders::add (song.edit, { &song.track ("D"), &song.track ("E") }, "Other");
        REQUIRE (other.has_value());
        folders::setFolder (song.track ("B"), other->id);
        CHECK (song.layout() == "[F A] C [D E B]");

        // The last track taken out: the folder is gone.
        folders::setFolder (song.track ("F"), {});
        folders::setFolder (song.track ("A"), {});
        CHECK (folders::getAll (song.edit).size() == 1);
        CHECK (song.layout() == "A F C [D E B]");

        folders::setFolder (song.track ("C"), "no such folder");
        CHECK_FALSE (folders::getFolderOf (song.track ("C")).has_value());
    }

    TEST_CASE ("after tracks move or go: one moved away leaves, one moved in between joins, an empty folder goes")
    {
        SixTracks song;
        const auto folder = folders::add (song.edit, { &song.track ("B"), &song.track ("C"), &song.track ("D") }, "Three");
        REQUIRE (folder.has_value());
        CHECK (song.layout() == "A [B C D] E F");

        // D moved to the end.
        song.edit.moveTrack (&song.track ("D"), te::TrackInsertPoint (nullptr, &song.track ("F")));
        folders::trackMoved (song.track ("D"));
        CHECK (song.layout() == "A [B C] E F D");

        // E moved between B and C.
        song.edit.moveTrack (&song.track ("E"), te::TrackInsertPoint (nullptr, &song.track ("B")));
        folders::trackMoved (song.track ("E"));
        CHECK (song.layout() == "A [B E C] F D");

        // The first of the folder moved away: the rest is still the folder.
        song.edit.moveTrack (&song.track ("B"), te::TrackInsertPoint (nullptr, &song.track ("D")));
        folders::trackMoved (song.track ("B"));
        CHECK (song.layout() == "A [E C] F D B");

        song.edit.deleteTrack (&song.track ("E"));
        song.edit.deleteTrack (&song.track ("C"));
        folders::tidy (song.edit);
        CHECK (song.layout() == "A F D B");
        CHECK (folders::getAll (song.edit).empty());
    }

    TEST_CASE ("a folder is muted and soloed as a whole, collapsed without an undo step, and saved with the song")
    {
        test::TempFolder temp;
        SixTracks song;
        const auto folder = folders::add (song.edit, { &song.track ("B"), &song.track ("C") }, "Pair");
        REQUIRE (folder.has_value());
        song.project->clearUndoHistory();

        CHECK_FALSE (folders::isMuted (song.edit, folder->id));
        folders::setMuted (song.edit, folder->id, true);
        CHECK (song.track ("B").isMuted (false));
        CHECK (song.track ("C").isMuted (false));
        CHECK_FALSE (song.track ("A").isMuted (false));
        CHECK (folders::isMuted (song.edit, folder->id));

        // One of them unmuted by hand: not all of it is muted.
        song.track ("C").setMute (false);
        CHECK_FALSE (folders::isMuted (song.edit, folder->id));
        folders::setMuted (song.edit, folder->id, false);
        CHECK_FALSE (song.track ("B").isMuted (false));

        folders::setSoloed (song.edit, folder->id, true);
        CHECK (folders::isSoloed (song.edit, folder->id));
        CHECK (song.track ("B").isSolo (false));
        folders::setSoloed (song.edit, folder->id, false);
        song.project->clearUndoHistory();

        folders::setCollapsed (song.edit, folder->id, true);
        CHECK (folders::getFolderOf (song.track ("B"))->collapsed);
        CHECK_FALSE (song.project->canUndo());

        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (song.project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

        const auto loadedFolders = folders::getAll (loaded->getEdit());
        REQUIRE (loadedFolders.size() == 1);
        CHECK (loadedFolders[0].name == "Pair");
        CHECK (loadedFolders[0].collapsed);
        const auto members = folders::getMembers (loaded->getEdit(), loadedFolders[0].id);
        REQUIRE (members.size() == 2);
        CHECK (members[0]->getName() == "B");
        CHECK (members[1]->getName() == "C");
    }
}
