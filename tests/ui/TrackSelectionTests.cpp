#include <doctest.h>

#include "TestSession.h"
#include "audio/Groups.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/TrackFolders.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/arrangement/TrackHeaders.h"

using namespace harmonia;
namespace folders = core::folders;
namespace groups = audio::groups;

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

    const juce::ModifierKeys plain (juce::ModifierKeys::leftButtonModifier);
    const juce::ModifierKeys ctrl (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
    const juce::ModifierKeys shift (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    const juce::ModifierKeys rightButton (juce::ModifierKeys::rightButtonModifier);

    /** A song of tracks A..E, each with a clip over its first bar, in the main window. */
    struct SelectionFixture
    {
        SelectionFixture()
        {
            project->getTracks().getFirst()->setName ("A");

            for (const auto* name : { "B", "C", "D", "E" })
                project->addTrack (name);

            for (auto* t : project->getTracks())
                project->addMidiClip (*t, 0.0, 4.0, t->getName());

            main = std::make_unique<ui::MainComponent> (test::session(), *project);
            main->setBounds (0, 0, 1280, 800);
            settle();
            project->clearUndoHistory();
        }

        ~SelectionFixture()    { main.reset(); }

        /** Lets the engine finish what it does a moment after a change (it puts a track's clips
            in order as an undo step of its own; in the program that stays in the command's step). */
        void settle()
        {
            dispatch();

            for (int i = 0; i < 2000 && project->isSettling(); ++i)
                dispatch (1);

            REQUIRE_FALSE (project->isSettling());
        }

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

        /** Presses the mouse on a track's header. */
        void press (const juce::String& name, const juce::ModifierKeys& mods)
        {
            headers().headerPressed (track (name), mods);
            dispatch();
        }

        static juce::String namesOf (const juce::Array<te::AudioTrack*>& tracks)
        {
            juce::StringArray names;

            for (auto* t : tracks)
                names.add (t->getName());

            return names.joinIntoString (" ");
        }

        juce::String selected()     { return namesOf (model().getSelectedTracks()); }
        juce::String rows()         { return namesOf (model().getTracks()); }

        juce::String muted()
        {
            juce::Array<te::AudioTrack*> tracks;

            for (auto* t : project->getTracks())
                if (t->isMuted (false))
                    tracks.add (t);

            return namesOf (tracks);
        }

        juce::String soloed()
        {
            juce::Array<te::AudioTrack*> tracks;

            for (auto* t : project->getTracks())
                if (t->isSolo (false))
                    tracks.add (t);

            return namesOf (tracks);
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main;
    };
}

TEST_SUITE ("arrangement: several tracks selected")
{
    TEST_CASE ("Ctrl + click takes a track in or out, Shift + click the tracks in between; a plain click leaves one")
    {
        SelectionFixture f;
        auto& model = f.model();
        f.press ("B", plain);
        CHECK (f.selected() == "B");
        CHECK (model.isTrackSelected (&f.track ("B")));
        CHECK_FALSE (model.isTrackSelected (&f.track ("A")));
        CHECK_FALSE (model.isTrackSelected (nullptr));
        auto* openClip = model.getOpenClip();

        // Ctrl: D joins; the current track and the clip open in the piano roll stay.
        f.press ("D", ctrl);
        CHECK (f.selected() == "B D");
        CHECK (model.getCurrentTrack() == &f.track ("B"));
        CHECK (model.getOpenClip() == openClip);
        CHECK (model.isTrackSelected (&f.track ("D")));
        CHECK (model.getTracksFor (f.track ("D")).size() == 2);
        CHECK (model.getTracksFor (f.track ("A")).size() == 1);         // not one of them: itself alone
        CHECK (model.getTracksFor (f.track ("A")).getFirst() == &f.track ("A"));

        // Ctrl again: out.
        f.press ("D", ctrl);
        CHECK (f.selected() == "B");

        // Shift: from the current track to the one clicked, either way.
        f.press ("D", shift);
        CHECK (f.selected() == "B C D");
        f.press ("A", shift);
        CHECK (f.selected() == "A B");
        CHECK (model.getCurrentTrack() == &f.track ("B"));

        // Ctrl on the current track: it goes out, the first of the others is current.
        f.press ("E", ctrl);
        CHECK (f.selected() == "A B E");
        f.press ("B", ctrl);
        CHECK (f.selected() == "A E");
        CHECK (model.getCurrentTrack() == &f.track ("A"));

        // Alone, Ctrl on the current track changes nothing.
        f.press ("E", ctrl);
        CHECK (f.selected() == "A");
        f.press ("A", ctrl);
        CHECK (f.selected() == "A");
        CHECK (model.getCurrentTrack() == &f.track ("A"));

        // A plain click: only that track; on the current one of several too.
        f.press ("C", ctrl);
        f.press ("D", plain);
        CHECK (f.selected() == "D");
        f.press ("E", ctrl);
        f.press ("D", plain);
        CHECK (f.selected() == "D");

        // A clip of another track opened: that track alone.
        f.press ("E", ctrl);
        model.openClip (dynamic_cast<te::MidiClip*> (core::clips::getClips (f.track ("A")).getFirst()));
        dispatch();
        CHECK (f.selected() == "A");

        // The right button: one of the selected keeps them all; another track becomes the only one.
        f.press ("C", ctrl);
        f.press ("C", rightButton);
        CHECK (f.selected() == "A C");
        CHECK (model.getCurrentTrack() == &f.track ("A"));
        f.press ("E", rightButton);
        CHECK (f.selected() == "E");

        // Selecting changes nothing in the song.
        CHECK_FALSE (f.project->canUndo());
        CHECK_FALSE (f.project->hasUnsavedChanges());

        // The window paints with several selected: A (current), B and D.
        f.press ("A", plain);
        f.press ("B", ctrl);
        f.press ("D", ctrl);
        CHECK (f.selected() == "A B D");
        juce::Image image (juce::Image::RGB, 1280, 800, true);

        {
            juce::Graphics g (image);
            f.main->paintEntireComponent (g, true);
        }

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-tracks-selected.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }

    TEST_CASE ("mute and solo on one of the selected tracks are for them all; on another track, for it alone")
    {
        SelectionFixture f;
        auto& headers = f.headers();
        f.press ("B", plain);
        f.press ("D", shift);
        REQUIRE (f.selected() == "B C D");

        // All go the way the clicked one goes.
        f.track ("C").setMute (true);
        REQUIRE (headers.getMuteButton (1) != nullptr);
        headers.getMuteButton (1)->onClick();           // B: not muted, so mute
        CHECK (f.muted() == "B C D");

        headers.getMuteButton (2)->onClick();           // C: muted, so unmute
        CHECK (f.muted().isEmpty());

        headers.getSoloButton (3)->onClick();           // D
        CHECK (f.soloed() == "B C D");
        headers.getSoloButton (1)->onClick();
        CHECK (f.soloed().isEmpty());

        // The menu's Mute and Solo do the same.
        choose (headers.createTrackMenu (f.track ("C")), "Mute");
        CHECK (f.muted() == "B C D");
        choose (headers.createTrackMenu (f.track ("D")), "Mute");
        CHECK (f.muted().isEmpty());
        choose (headers.createTrackMenu (f.track ("B")), "Solo");
        CHECK (f.soloed() == "B C D");
        choose (headers.createTrackMenu (f.track ("B")), "Solo");
        CHECK (f.soloed().isEmpty());

        // A track that isn't one of them: itself alone, and the selection stays.
        headers.getMuteButton (4)->onClick();           // E
        CHECK (f.muted() == "E");
        headers.getSoloButton (0)->onClick();           // A
        CHECK (f.soloed() == "A");
        dispatch();
        CHECK (f.selected() == "B C D");
    }

    TEST_CASE ("the menu of one of the selected tracks: colour, output, folder and delete for them all, one undo step each")
    {
        SelectionFixture f;
        auto& model = f.model();
        auto& headers = f.headers();
        f.press ("B", plain);
        f.press ("D", shift);
        REQUIRE (f.selected() == "B C D");

        // It says how many, and has only what is done to them all.
        auto menu = headers.createTrackMenu (f.track ("C"));
        CHECK (find (menu, "3 tracks selected").has_value());
        CHECK (find (menu, "Delete 3 tracks").has_value());
        CHECK (find (menu, "New group with these tracks").has_value());
        CHECK (find (menu, "New folder with these tracks").has_value());
        CHECK_FALSE (find (menu, "Rename").has_value());
        CHECK_FALSE (find (menu, "Duplicate track").has_value());
        CHECK_FALSE (find (menu, "Freeze").has_value());

        // Another track's menu is its own, whole.
        const auto own = headers.createTrackMenu (f.track ("A"));
        CHECK (find (own, "Rename").has_value());
        CHECK (find (own, "Delete track").has_value());
        CHECK (find (own, "New group with this track").has_value());

        // The Track menu of the menu bar is the current track's: the same.
        const auto bar = f.main->getMainMenu().createTrackMenu();
        CHECK (find (bar, "3 tracks selected").has_value());
        CHECK_FALSE (find (bar, "\"B\"").has_value());

        // In the mixer a track's output is its own, selected or not.
        CHECK (find (ui::arrangement::makeOutputMenu (model, f.track ("C")), "New group with this track").has_value());

        // Colour.
        model.setTracksColour (f.track ("C"), juce::Colours::red);
        CHECK (core::tracks::getColour (f.track ("B")) == juce::Colours::red);
        CHECK (core::tracks::getColour (f.track ("D")) == juce::Colours::red);
        CHECK_FALSE (core::tracks::getColour (f.track ("A")).has_value());
        f.project->undo();
        CHECK_FALSE (core::tracks::getColour (f.track ("B")).has_value());
        CHECK_FALSE (core::tracks::getColour (f.track ("D")).has_value());
        CHECK_FALSE (f.project->canUndo());

        // A new group they all play through.
        choose (headers.createTrackMenu (f.track ("C")), "New group with these tracks");
        REQUIRE (f.project->getGroupTracks().size() == 1);
        auto* group = f.project->getGroupTracks().getFirst();
        CHECK (groups::getGroup (f.track ("B")) == group);
        CHECK (groups::getGroup (f.track ("C")) == group);
        CHECK (groups::getGroup (f.track ("D")) == group);
        CHECK (groups::getGroup (f.track ("A")) == nullptr);
        dispatch();
        CHECK (f.selected() == "B C D");

        // Back to the master, then into the group again: each one undo step.
        f.project->clearUndoHistory();
        choose (headers.createTrackMenu (f.track ("D")), "Master");
        CHECK (groups::getGroup (f.track ("B")) == nullptr);
        CHECK (groups::getGroup (f.track ("D")) == nullptr);
        choose (headers.createTrackMenu (f.track ("D")), group->getName());
        CHECK (groups::getGroup (f.track ("C")) == group);
        f.project->undo();
        CHECK (groups::getGroup (f.track ("C")) == nullptr);
        f.project->undo();
        CHECK (groups::getGroup (f.track ("B")) == group);
        CHECK_FALSE (f.project->canUndo());

        // A folder of them; out of it again.
        choose (headers.createTrackMenu (f.track ("B")), "New folder with these tracks");
        dispatch();
        REQUIRE (folders::getAll (f.project->getEdit()).size() == 1);
        const auto folder = folders::getAll (f.project->getEdit()).front();
        CHECK (folders::getMembers (f.project->getEdit(), folder.id).size() == 3);
        CHECK_FALSE (model.getFolderOf (f.track ("A")).has_value());
        REQUIRE (f.selected() == "B C D");

        f.project->clearUndoHistory();
        choose (headers.createTrackMenu (f.track ("C")), "Take out of the folder");
        dispatch();
        CHECK_FALSE (model.getFolderOf (f.track ("B")).has_value());
        CHECK_FALSE (model.getFolderOf (f.track ("C")).has_value());
        CHECK_FALSE (model.getFolderOf (f.track ("D")).has_value());
        f.project->undo();
        dispatch();
        CHECK (folders::getMembers (f.project->getEdit(), folder.id).size() == 3);
        CHECK_FALSE (f.project->canUndo());

        // Delete them: the track that comes to the current one's row is current.
        f.press ("B", plain);
        f.press ("D", shift);
        f.project->clearUndoHistory();
        choose (headers.createTrackMenu (f.track ("C")), "Delete 3 tracks");
        f.settle();
        CHECK (f.rows() == "A E");
        CHECK (f.selected() == "E");
        CHECK (f.project->getUndoDescription() == "Delete tracks");

        f.project->undo();
        f.settle();
        CHECK (f.rows() == "A B C D E");
        CHECK (core::clips::getClips (f.track ("C")).size() == 1);
        CHECK_FALSE (f.project->canUndo());

        // And again (the tracks were in a folder: the engine putting the song's nodes in order
        // after the undo used to drop the redo).
        REQUIRE (f.project->canRedo());
        f.project->redo();
        f.settle();
        CHECK (f.rows() == "A E");
    }
}
