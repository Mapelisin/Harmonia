#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "audio/Instruments.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/arrangement/ArrangementGestures.h"

using namespace harmonia;
using namespace harmonia::ui::arrangement;
namespace clips = core::clips;

namespace
{
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    constexpr int ctrl = juce::ModifierKeys::ctrlModifier;
    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;

    void dispatch (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** Rows: 0 "Keys" (notes: a clip over beats 0..16 with a note on every beat), 1 "Bass" (notes:
        a clip over 4..12), 2 "Guitar" (sound: an 8-beat clip at 0, the song at 120 BPM). The
        arrangement at 16 pixels per beat, snapping to beats. */
    struct TimeFixture
    {
        TimeFixture()
        {
            keys = project->getTracks().getFirst();
            keys->setName ("Keys");
            keysClip = project->addMidiClip (*keys, 0.0, 16.0, "Chords");
            std::vector<core::Note> notes;

            for (int beat = 0; beat < 16; ++beat)
            {
                core::Note n;
                n.pitch = 60 + beat;
                n.startBeat = beat;
                n.lengthBeats = 1.0;
                notes.push_back (n);
            }

            core::NoteEditor (*keysClip).add (notes);

            bass = project->addTrack ("Bass");
            REQUIRE (audio::instruments::useBuiltInSynth (*bass).isEmpty());
            bassClip = project->addMidiClip (*bass, 4.0, 8.0, "Line");

            guitar = project->addTrack ("Guitar");
            core::tracks::setKind (*guitar, core::tracks::Kind::audio);
            juce::String error;
            guitarClip = project->addAudioClip (*guitar, test::writeTone (temp.getFile ("Riff.wav"), 4.0, 0.4f), 0.0, error);
            REQUIRE_MESSAGE (guitarClip != nullptr, error.toStdString());

            view.width = 1000.0f;
            view.height = 400.0f;
            view.pixelsPerBeat = 16.0;
            view.numTracks = model.getNumTracks();
            model.setPixelsPerBeat (view.pixelsPerBeat);
            model.setSnap (core::snap::Mode::beat);
            dispatch();
            project->clearUndoHistory();
        }

        /** A point in a row, below where a clip's name strip is. */
        juce::Point<float> at (double beat, int row, float yInRow = 34.0f) const
        {
            return { view.beatToX (beat), view.trackToY (row) + yInRow };
        }

        /** A point on a clip's name strip. */
        juce::Point<float> onName (double beat, int row) const      { return at (beat, row, 7.0f); }

        void click (juce::Point<float> position, int mods = 0)
        {
            gestures.down ({ position, juce::ModifierKeys (mods | left), 1 });
            gestures.up ({ position, juce::ModifierKeys (mods | left), 1 });
        }

        void drag (juce::Point<float> from, juce::Point<float> to, int mods = 0)
        {
            const juce::ModifierKeys keysDown (mods | left);
            gestures.down ({ from, keysDown });

            for (int i = 1; i <= 10; ++i)
                gestures.drag ({ from + (to - from) * ((float) i / 10.0f), keysDown });

            gestures.up ({ to, keysDown });
        }

        /** "0-4, 8-16": where the track's clips are, in beats. */
        juce::String spans (te::AudioTrack& track) const
        {
            juce::StringArray result;

            for (auto* clip : clips::getClips (track))
                result.add (juce::String (clips::getStartBeat (*clip), 2).upToLastOccurrenceOf (".00", false, false) + "-"
                              + juce::String (clips::getEndBeat (*clip), 2).upToLastOccurrenceOf (".00", false, false));

            return result.joinIntoString (", ");
        }

        /** The pitches of the notes the track's clips play, in song order, with the beat each starts on. */
        juce::String notesOf (te::AudioTrack& track) const
        {
            std::vector<std::pair<double, int>> played;

            for (auto* clip : clips::getMidiClips (track))
                for (const auto& note : clips::getPlayedNotes (*clip))
                    played.push_back ({ note.startBeat, note.note.pitch });

            std::sort (played.begin(), played.end());
            juce::StringArray result;

            for (const auto& [beat, pitch] : played)
                result.add (juce::String (pitch) + "@" + juce::String (juce::roundToInt (beat)));

            return result.joinIntoString (" ");
        }

        juce::String piece() const
        {
            const auto p = model.getTimeSelection();
            return p.has_value() ? juce::String (p->startBeat, 2).upToLastOccurrenceOf (".00", false, false) + "-"
                                     + juce::String (p->endBeat, 2).upToLastOccurrenceOf (".00", false, false)
                                     + " rows " + juce::String (p->firstRow) + "-" + juce::String (p->lastRow)
                                 : juce::String ("none");
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* keys = nullptr;
        te::AudioTrack* bass = nullptr;
        te::AudioTrack* guitar = nullptr;
        te::MidiClip* keysClip = nullptr;
        te::MidiClip* bassClip = nullptr;
        te::Clip* guitarClip = nullptr;
        ArrangementModel model { *project };
        ArrangementView view;
        ArrangementGestures gestures { model, view };
    };
}

TEST_SUITE ("arrangement: a piece of time")
{
    TEST_CASE ("a drag in the rows selects a piece of time, whatever the clips' borders; a clip is taken by its name")
    {
        TimeFixture f;

        // Across a clip, from inside it to past its end, on one row: on the grid.
        f.drag (f.at (6.3, 0), f.at (9.6, 0));
        CHECK (f.piece() == "6-10 rows 0-0");
        CHECK (f.model.getSelectedClips().isEmpty());
        CHECK (f.spans (*f.keys) == "0-16");                    // nothing moved, nothing cut
        CHECK_FALSE (f.project->canUndo());

        // Backwards, over three rows, starting on empty space (outside the piece there is: inside
        // it, a drag would take the piece); Alt: off the grid.
        f.drag (f.at (14.0, 2), f.at (2.0, 0));
        CHECK (f.piece() == "2-14 rows 0-2");
        f.model.deselectAll();
        f.drag (f.at (5.25, 1), f.at (7.5, 1), alt);
        CHECK (f.piece() == "5.25-7.50 rows 1-1");

        // Beyond the rows and before the song: as far as they go.
        f.drag (f.at (3.0, 1), f.at (-50.0, 9));
        CHECK (f.piece() == "0-3 rows 1-2");
        CHECK (f.spans (*f.keys) == "0-16");
        CHECK (f.spans (*f.bass) == "4-12");
        CHECK_FALSE (f.project->canUndo());

        // A click lets it go; on a clip, the clip is selected and open as before.
        f.click (f.at (2.0, 0));
        CHECK (f.piece() == "none");
        CHECK (f.model.getSelectedClips() == juce::Array<te::Clip*> { f.keysClip });
        CHECK (f.model.getOpenClip() == f.keysClip);

        f.drag (f.at (6.0, 0), f.at (10.0, 0));
        CHECK (f.model.getSelectedClips().isEmpty());           // one kind of selection at a time
        f.click (f.at (20.0, 1));                               // empty space
        CHECK (f.piece() == "none");

        // A clip's name strip still takes the clip.
        f.drag (f.onName (6.0, 1), f.onName (10.0, 1));
        CHECK (f.piece() == "none");
        CHECK (f.spans (*f.bass) == "8-16");
        CHECK (f.project->getUndoDescription() == "Move clips");
        f.project->undo();
        dispatch();

        // Shift on empty space: whole clips, with a rectangle.
        f.drag (f.at (30.0, 0), f.at (10.0, 1), shift);
        CHECK (f.piece() == "none");
        CHECK (f.model.getSelectedClips().size() == 2);

        // Selecting clips clears a piece; Esc (deselect) clears both.
        f.model.setTimeSelection (2.0, 6.0, 0, 0);
        CHECK (f.model.getSelectedClips().isEmpty());
        f.model.setSelection ({ f.bassClip });
        CHECK (f.piece() == "none");
        f.model.setTimeSelection (2.0, 6.0, 0, 0);
        f.model.deselectAll();
        CHECK (f.piece() == "none");

        // A piece of no length is none.
        f.model.setTimeSelection (4.0, 4.0, 0, 1);
        CHECK (f.piece() == "none");
    }

    TEST_CASE ("Del takes what the piece has out of the rows: clips are cut at its edges, one undo step")
    {
        TimeFixture f;

        // The middle of a clip, on one row: it becomes two, the notes between are gone.
        f.model.setTimeSelection (6.0, 10.0, 0, 0);
        CHECK (f.model.deleteTime() == 1);
        dispatch();
        CHECK (f.spans (*f.keys) == "0-6, 10-16");
        CHECK (f.notesOf (*f.keys) == "60@0 61@1 62@2 63@3 64@4 65@5 70@10 71@11 72@12 73@13 74@14 75@15");
        CHECK (f.spans (*f.bass) == "4-12");
        CHECK (f.project->getUndoDescription() == "Delete time");
        CHECK (f.piece() == "6-10 rows 0-0");

        f.project->undo();
        dispatch();
        CHECK (f.spans (*f.keys) == "0-16");
        CHECK (f.notesOf (*f.keys).startsWith ("60@0 61@1 62@2 63@3 64@4 65@5 66@6 67@7"));
        CHECK_FALSE (f.project->canUndo());

        // Three rows at once, notes and sound: the start of one clip, the end of another, a whole one.
        f.model.setTimeSelection (2.0, 14.0, 0, 2);
        CHECK (f.model.deleteTime() == 3);
        dispatch();
        CHECK (f.spans (*f.keys) == "0-2, 14-16");
        CHECK (f.spans (*f.bass).isEmpty());
        CHECK (f.spans (*f.guitar) == "0-2");
        f.project->undo();
        dispatch();
        CHECK (f.spans (*f.bass) == "4-12");
        CHECK (f.spans (*f.guitar) == "0-8");

        // Nothing in it: nothing happens, nothing to undo.
        f.model.setTimeSelection (20.0, 24.0, 0, 2);
        CHECK (f.model.deleteTime() == 0);
        CHECK_FALSE (f.project->canUndo());

        // No piece: nothing.
        f.model.clearTimeSelection();
        CHECK (f.model.deleteTime() == 0);
        CHECK (f.model.copyTime() == 0);
        CHECK (f.model.duplicateTime() == 0);
        CHECK (f.model.splitTime() == 0);
    }

    TEST_CASE ("copy, cut and paste a piece of time; repeat it; split at its edges")
    {
        TimeFixture f;

        // Copy: the song isn't touched.
        f.model.setTimeSelection (2.0, 6.0, 0, 1);
        CHECK (f.model.copyTime() == 2);
        CHECK (ArrangementModel::hasCopiedTime());
        CHECK (f.spans (*f.keys) == "0-16");
        CHECK_FALSE (f.project->canUndo());

        // Pasted at beat 20, from the first row down: each row gets its part, cut to the piece.
        CHECK (f.model.paste (20.0, 0) == 2);
        dispatch();
        CHECK (f.spans (*f.keys) == "0-16, 20-24");
        CHECK (f.spans (*f.bass) == "4-12, 22-24");
        CHECK (f.notesOf (*f.keys).endsWith ("75@15 62@20 63@21 64@22 65@23"));
        CHECK (f.piece() == "20-24 rows 0-1");
        CHECK (f.project->getUndoDescription() == "Paste time");

        // Pasted over clips: in place of what was there.
        CHECK (f.model.paste (8.0, 0) == 2);
        dispatch();
        CHECK (f.spans (*f.keys) == "0-8, 8-12, 12-16, 20-24");
        CHECK (f.spans (*f.bass) == "4-8, 10-12, 22-24");
        f.project->undo();
        f.project->undo();
        dispatch();
        CHECK (f.spans (*f.keys) == "0-16");
        CHECK (f.spans (*f.bass) == "4-12");

        // Cut: copied, and gone; one undo step.
        f.model.setTimeSelection (0.0, 4.0, 2, 2);
        CHECK (f.model.cutTime() == 1);
        dispatch();
        CHECK (f.spans (*f.guitar) == "4-8");
        CHECK (f.project->getUndoDescription() == "Cut time");

        // Sound pasted onto a row of notes isn't (and the row keeps what it has there); onto
        // its own kind it is.
        CHECK (f.model.paste (2.0, 0) == 0);
        CHECK (f.spans (*f.keys) == "0-16");
        CHECK (f.project->getUndoDescription() == "Cut time");
        CHECK (f.model.paste (12.0, 2) == 1);
        dispatch();
        CHECK (f.spans (*f.guitar) == "4-8, 12-16");
        f.project->undo();
        f.project->undo();
        dispatch();
        CHECK (f.spans (*f.guitar) == "0-8");

        // Repeat: once more right after it, in place of what was there; and again further on.
        f.model.setTimeSelection (0.0, 2.0, 0, 0);
        CHECK (f.model.duplicateTime() == 1);
        dispatch();
        CHECK (f.piece() == "2-4 rows 0-0");
        CHECK (f.notesOf (*f.keys).startsWith ("60@0 61@1 60@2 61@3 64@4"));
        CHECK (f.project->getUndoDescription() == "Duplicate time");
        CHECK (f.model.duplicateTime() == 1);
        dispatch();
        CHECK (f.notesOf (*f.keys).startsWith ("60@0 61@1 60@2 61@3 60@4 61@5 66@6"));
        f.project->undo();
        f.project->undo();
        dispatch();
        CHECK (f.spans (*f.keys) == "0-16");

        // Split: the clips are cut at both edges, where an edge goes through one.
        f.model.setTimeSelection (2.0, 6.0, 0, 2);
        CHECK (f.model.splitTime() == 5);
        dispatch();
        CHECK (f.spans (*f.keys) == "0-2, 2-6, 6-16");
        CHECK (f.spans (*f.bass) == "4-6, 6-12");
        CHECK (f.spans (*f.guitar) == "0-2, 2-6, 6-8");
        CHECK (f.notesOf (*f.keys).startsWith ("60@0 61@1 62@2 63@3 64@4 65@5 66@6"));      // nothing lost
        CHECK (f.model.splitTime() == 0);
        f.project->undo();
        dispatch();
        CHECK (f.spans (*f.guitar) == "0-8");

        // Whole clips copied afterwards paste as clips again.
        f.model.setSelection ({ f.bassClip });
        CHECK (f.model.copySelected() == 1);
        CHECK_FALSE (ArrangementModel::hasCopiedTime());
    }

    TEST_CASE ("the selected piece is dragged to another place or row: what it has moves there (Ctrl: a copy), in place of what was there")
    {
        TimeFixture f;

        // Beats 8..12 of the first row, to the second row and four beats later.
        f.drag (f.at (8.0, 0), f.at (12.0, 0));
        REQUIRE (f.piece() == "8-12 rows 0-0");

        // While it is dragged: where it would land. Nothing changes until the button is let go.
        const juce::ModifierKeys keysDown (left);
        f.gestures.down ({ f.at (9.0, 0), keysDown });
        f.gestures.drag ({ f.at (11.0, 1), keysDown });
        f.gestures.drag ({ f.at (13.2, 1), keysDown });
        REQUIRE (f.gestures.getTimeMoveTarget().has_value());
        CHECK (f.gestures.getTimeMoveTarget()->startBeat == doctest::Approx (12.0));
        CHECK (f.gestures.getTimeMoveTarget()->firstRow == 1);
        CHECK (f.spans (*f.keys) == "0-16");
        f.gestures.up ({ f.at (13.2, 1), keysDown });
        dispatch();

        CHECK (f.spans (*f.keys) == "0-8, 12-16");
        CHECK (f.spans (*f.bass) == "4-12, 12-16");
        CHECK (f.notesOf (*f.bass) == "68@12 69@13 70@14 71@15");
        CHECK (f.piece() == "12-16 rows 1-1");
        CHECK (f.project->getUndoDescription() == "Move time");
        CHECK_FALSE (f.gestures.getTimeMoveTarget().has_value());

        f.project->undo();
        dispatch();
        CHECK (f.spans (*f.keys) == "0-16");
        CHECK (f.spans (*f.bass) == "4-12");
        CHECK_FALSE (f.project->canUndo());

        // Ctrl: a copy; over what is there: in its place.
        f.model.setTimeSelection (0.0, 4.0, 0, 0);
        f.drag (f.at (1.0, 0), f.at (9.0, 0), ctrl);
        dispatch();
        CHECK (f.piece() == "8-12 rows 0-0");
        CHECK (f.spans (*f.keys) == "0-8, 8-12, 12-16");
        CHECK (f.notesOf (*f.keys) == "60@0 61@1 62@2 63@3 64@4 65@5 66@6 67@7 60@8 61@9 62@10 63@11 72@12 73@13 74@14 75@15");
        CHECK (f.project->getUndoDescription() == "Copy time");
        f.project->undo();
        dispatch();

        // Notes can't go onto a row of sound: said, nothing changed.
        juce::String problem;
        f.gestures.onProblem = [&problem] (const juce::String& text) { problem = text; };
        f.model.setTimeSelection (0.0, 4.0, 1, 1);
        f.model.setTimeSelection (4.0, 8.0, 1, 1);
        f.drag (f.at (5.0, 1), f.at (5.0, 2));
        CHECK (problem == "Notes go on instrument tracks, sound on audio tracks");
        CHECK (f.spans (*f.bass) == "4-12");
        CHECK (f.spans (*f.guitar) == "0-8");
        CHECK_FALSE (f.project->canUndo());

        // By the model: before the song's start, off the rows.
        CHECK (f.model.moveTime (-10.0, 0, false).isNotEmpty());
        CHECK (f.model.moveTime (0.0, 5, false).isNotEmpty());
        CHECK (f.model.moveTime (0.0, 0, false).isEmpty());
        CHECK_FALSE (f.project->canUndo());

        // A click inside the piece (no drag) lets it go.
        f.click (f.at (5.0, 1));
        CHECK (f.piece() == "none");
    }

    TEST_CASE ("the keys act on the selected time: Del, Ctrl+X / C / V, Ctrl+B, Ctrl+E, Ctrl+L, Esc; the Edit menu says so")
    {
        TimeFixture f;
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& arrangement = main->getArrangement();
        auto& model = main->getArrangementModel();
        dispatch();
        f.project->clearUndoHistory();

        const auto press = [&arrangement] (int code, int mods = 0)
        {
            return arrangement.handleShortcut (juce::KeyPress (code, juce::ModifierKeys (mods), 0));
        };

        juce::String said;
        arrangement.onMessage = [&said] (const juce::String& text, bool) { said = text; };

        // Set HARMONIA_SNAPSHOT_DIR to look at a piece of time selected over two rows.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            model.setTimeSelection (2.0, 7.0, 0, 1);
            dispatch();
            const auto file = juce::File (dir).getChildFile ("main-time-selection.png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (main->createComponentSnapshot (main->getLocalBounds()), stream);
        }

        model.setTimeSelection (4.0, 8.0, 0, 0);
        CHECK (press ('c', ctrl));
        CHECK (said.startsWith ("Copied the selected time"));
        CHECK (press (juce::KeyPress::deleteKey));
        dispatch();
        CHECK (f.spans (*f.keys) == "0-4, 8-16");
        CHECK (f.project->getUndoDescription() == "Delete time");

        // Paste where time is selected: at its start, on its rows.
        model.setTimeSelection (20.0, 21.0, 0, 0);
        CHECK (press ('v', ctrl));
        dispatch();
        CHECK (f.spans (*f.keys) == "0-4, 8-16, 20-24");
        CHECK (said.startsWith ("Pasted the copied time"));

        // Repeat, split, loop.
        CHECK (press ('b', ctrl));
        dispatch();
        CHECK (f.spans (*f.keys) == "0-4, 8-16, 20-24, 24-28");
        model.setTimeSelection (10.0, 12.0, 0, 0);
        CHECK (press ('e', ctrl));
        dispatch();
        CHECK (f.spans (*f.keys) == "0-4, 8-10, 10-12, 12-16, 20-24, 24-28");
        CHECK (press ('l', ctrl));
        REQUIRE (model.getLoopRegion().has_value());
        CHECK (model.getLoopRegion()->start == doctest::Approx (10.0));
        CHECK (model.getLoopRegion()->end == doctest::Approx (12.0));

        // Cut; nothing in the piece: said.
        CHECK (press ('x', ctrl));
        dispatch();
        CHECK (f.spans (*f.keys) == "0-4, 8-10, 12-16, 20-24, 24-28");
        CHECK (press ('x', ctrl));
        CHECK (said.startsWith ("Nothing to cut"));

        // The Edit menu acts on the time, and says so (the arrangement is what Edit is about
        // while an audio track is the current one).
        model.setCurrentTrack (f.guitar);
        dispatch();
        REQUIRE (main->isArrangementTheEditTarget());
        juce::StringArray headers;
        std::optional<juce::PopupMenu::Item> deleteItem;
        const auto menu = main->getMainMenu().getMenuForIndex (1, "Edit");

        for (juce::PopupMenu::MenuItemIterator it (menu, false); it.next();)
        {
            if (it.getItem().isSectionHeader)
                headers.add (it.getItem().text);

            if (it.getItem().text == "Delete")
                deleteItem = it.getItem();
        }

        CHECK (headers.contains ("The selected time in the arrangement"));
        REQUIRE (deleteItem.has_value());
        CHECK (deleteItem->isEnabled);

        // Esc lets the piece go; without one the keys act on clips as before.
        CHECK (press (juce::KeyPress::escapeKey));
        CHECK_FALSE (model.getTimeSelection().has_value());
        CHECK_FALSE (press ('l', ctrl));

        main.reset();
    }

    TEST_CASE ("frozen tracks and folded folders are left alone; rows that go away take the piece with them")
    {
        TimeFixture f;
        f.project->beginGesture ("Delete track");
        f.model.setTimeSelection (0.0, 8.0, 1, 2);
        f.model.deleteTrack (*f.guitar);
        dispatch();
        f.view.numTracks = f.model.getNumTracks();
        CHECK (f.piece() == "0-8 rows 1-1");
        f.model.deleteTrack (*f.bass);
        dispatch();
        CHECK (f.piece() == "none");
    }
}
