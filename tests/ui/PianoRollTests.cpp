#include <doctest.h>

#include "core/TestProject.h"
#include "core/Clips.h"
#include "ui/pianoroll/GridGestures.h"

using namespace harmonia;
using namespace harmonia::ui::pianoroll;
using core::Note;
namespace snap = core::snap;

namespace
{
    struct RecordingAuditioner final : Auditioner
    {
        void noteOn (int pitch, int) override   { sounding.push_back (pitch); ++started; }
        void noteOff (int pitch) override       { std::erase (sounding, pitch); }

        std::vector<int> sounding;
        int started = 0;
    };

    /** A 4-bar clip in the piano roll, 100 pixels per beat, 10 pixels per row. */
    struct PianoRollFixture : test::ProjectWithClip
    {
        PianoRollFixture()
        {
            model.setClip (clip);
            model.setAuditioner (&auditioner);
            model.setSnap (snap::Mode::quarterBeat);

            view.width = 1600.0f;
            view.height = 900.0f;
            view.pixelsPerBeat = 100.0;
            view.rowHeight = 10.0f;
            view.topRow = 40.0f;     // pitch 87 at the top
            model.setPixelsPerBeat (view.pixelsPerBeat);
        }

        ~PianoRollFixture()
        {
            model.setAuditioner (nullptr);
        }

        juce::Point<float> at (double beat, int pitch) const
        {
            return { view.beatToX (beat), view.pitchToY (pitch) + view.rowHeight * 0.5f };
        }

        static GridGestures::Pointer pointer (juce::Point<float> p, int flags = 0, int clicks = 1)
        {
            return { p, juce::ModifierKeys (flags), clicks };
        }

        void press (juce::Point<float> p, int flags = juce::ModifierKeys::leftButtonModifier)
        {
            gestures.down (pointer (p, flags));
        }

        void dragTo (juce::Point<float> p, int flags = juce::ModifierKeys::leftButtonModifier)
        {
            gestures.drag (pointer (p, flags));
        }

        void release (juce::Point<float> p, int flags = 0)
        {
            gestures.up (pointer (p, flags));
        }

        void click (juce::Point<float> p, int extraFlags = 0)
        {
            press (p, juce::ModifierKeys::leftButtonModifier | extraFlags);
            release (p, extraFlags);
        }

        void drag (juce::Point<float> from, juce::Point<float> to, int extraFlags = 0,
                   int button = juce::ModifierKeys::leftButtonModifier)
        {
            press (from, button | extraFlags);

            // A few intermediate events, like a real mouse.
            for (int i = 1; i <= 4; ++i)
                dragTo (from + (to - from) * ((float) i / 4.0f), button | extraFlags);

            release (to, extraFlags);
        }

        te::MidiNote* addNote (int pitch, double start, double length, int velocity = 100)
        {
            Note n;
            n.pitch = pitch;
            n.startBeat = start;
            n.lengthBeats = length;
            n.velocity = velocity;
            model.beginGesture ("test");
            return model.addNote (n);
        }

        std::vector<Note> notes()
        {
            std::vector<Note> result;

            for (const auto& d : model.getNotes())
                result.push_back (d.values);

            return result;
        }

        RecordingAuditioner auditioner;
        PianoRollModel model { *project };
        View view;
        GridGestures gestures { model, view };
    };

    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int ctrl = juce::ModifierKeys::ctrlModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;
    constexpr int rightButton = juce::ModifierKeys::rightButtonModifier;
}

TEST_SUITE ("piano roll: drawing and editing with the mouse")
{
    TEST_CASE ("a click adds a note at the start of the grid cell, with the last used length, as one undo step")
    {
        PianoRollFixture f;
        f.model.setNewNoteLength (0.5);

        f.press (f.at (1.37, 64));
        CHECK (f.auditioner.sounding == std::vector<int> { 64 });
        f.release (f.at (1.37, 64));
        CHECK (f.auditioner.sounding.empty());

        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].pitch == 64);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.25));
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (0.5));

        f.project->undo();
        CHECK (f.notes().empty());
    }

    TEST_CASE ("a new note follows the mouse until the button is released, still one undo step")
    {
        PianoRollFixture f;
        f.model.setSnap (snap::Mode::beat);

        f.drag (f.at (1.1, 60), f.at (2.5, 62));

        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (2.0));
        CHECK (f.notes()[0].pitch == 62);
        CHECK (f.auditioner.started >= 2);     // heard at 60, then on the way to 62
        CHECK (f.auditioner.sounding.empty());

        f.project->undo();
        CHECK (f.notes().empty());
    }

    TEST_CASE ("clicking a note sets the length of the next new note")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.5);

        f.click (f.at (0.5, 60));
        CHECK (f.model.getNewNoteLength() == doctest::Approx (1.5));
        CHECK (f.notes().size() == 1);

        f.click (f.at (2.1, 67));
        REQUIRE (f.notes().size() == 2);
        CHECK (f.notes()[1].lengthBeats == doctest::Approx (1.5));
    }

    TEST_CASE ("dragging a selected note moves the whole selection; Alt turns snap off")
    {
        PianoRollFixture f;
        auto* a = f.addNote (60, 1.0, 0.5);
        auto* b = f.addNote (64, 2.0, 0.5);
        f.model.setSelection ({ a, b });

        f.drag (f.at (1.2, 60), f.at (2.2, 62));
        auto n = f.notes();
        CHECK (n[0].startBeat == doctest::Approx (2.0));
        CHECK (n[0].pitch == 62);
        CHECK (n[1].startBeat == doctest::Approx (3.0));
        CHECK (n[1].pitch == 66);

        // Without snap: moved by exactly the mouse distance (positions are whole ticks, 1/960 beat).
        f.drag (f.at (2.2, 62), f.at (2.325, 62), alt);
        n = f.notes();
        CHECK (n[0].startBeat == doctest::Approx (2.125));
        CHECK (n[1].startBeat == doctest::Approx (3.125));

        // The selection survives undo of the moves.
        f.project->undo();
        f.project->undo();
        CHECK (f.model.getNumSelected() == 2);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.0));
    }

    TEST_CASE ("moving a note only up or down never quantizes its time")
    {
        PianoRollFixture f;
        f.addNote (60, 1.03125, 0.5);    // off the grid

        f.drag (f.at (1.2, 60), f.at (1.22, 63));
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.03125));
        CHECK (f.notes()[0].pitch == 63);
    }

    TEST_CASE ("dragging the right edge changes the length on the grid")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);

        auto edge = f.at (1.0, 60);
        edge.x -= 2.0f;
        f.drag (edge, f.at (2.3, 60));

        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (0.0));
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (2.25));
        CHECK (f.model.getNewNoteLength() == doctest::Approx (2.25));
    }

    TEST_CASE ("Shift+drag copies notes")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);

        f.drag (f.at (0.3, 60), f.at (2.3, 60), shift);

        const auto n = f.notes();
        REQUIRE (n.size() == 2);
        CHECK (n[0].startBeat == doctest::Approx (0.0));
        CHECK (n[1].startBeat == doctest::Approx (2.0));

        f.project->undo();
        CHECK (f.notes().size() == 1);
    }

    TEST_CASE ("Shift+drag on empty space draws a note as long as the drag")
    {
        PianoRollFixture f;
        f.drag (f.at (1.1, 60), f.at (2.6, 60), shift);

        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.0));
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (1.5));
    }

    TEST_CASE ("the right button deletes every note it passes over")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 0.5);
        f.addNote (60, 1.0, 0.5);
        f.addNote (60, 2.0, 0.5);
        f.addNote (72, 1.0, 0.5);

        f.drag (f.at (0.2, 60), f.at (2.2, 60), 0, rightButton);
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].pitch == 72);

        f.project->undo();
        CHECK (f.notes().size() == 4);
    }

    TEST_CASE ("Paint draws a row of notes along the drag, with no gaps")
    {
        PianoRollFixture f;
        f.model.setTool (Tool::paint);
        f.model.setNewNoteLength (0.5);

        f.press (f.at (0.1, 60));
        f.dragTo (f.at (2.2, 60));   // one fast move over several cells
        f.release (f.at (2.2, 60));

        const auto n = f.notes();
        REQUIRE (n.size() == 5);

        for (size_t i = 0; i < n.size(); ++i)
        {
            CHECK (n[i].startBeat == doctest::Approx (0.5 * (double) i));
            CHECK (n[i].lengthBeats == doctest::Approx (0.5));
        }

        f.project->undo();
        CHECK (f.notes().empty());
    }

    TEST_CASE ("the Delete tool removes clicked notes; the Mute tool toggles each note once per stroke")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);
        f.addNote (60, 2.0, 1.0);

        f.model.setTool (Tool::mute);
        f.press (f.at (0.5, 60));
        f.dragTo (f.at (2.5, 60));
        f.dragTo (f.at (0.5, 60));   // back over the same notes
        f.release (f.at (0.5, 60));
        CHECK (f.notes()[0].muted);
        CHECK (f.notes()[1].muted);

        f.model.setTool (Tool::erase);
        f.click (f.at (2.5, 60));
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (0.0));
    }

    TEST_CASE ("Slice cuts every note the line crosses, at the grid")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 4.0);
        f.addNote (64, 0.0, 4.0);
        f.addNote (72, 0.0, 4.0);     // not crossed

        f.model.setTool (Tool::slice);
        f.drag (f.at (2.04, 66), f.at (2.04, 58));

        const auto n = f.notes();
        REQUIRE (n.size() == 5);

        int cutAtTwo = 0;

        for (const auto& note : n)
            if (note.pitch != 72)
                cutAtTwo += (std::abs (note.startBeat - 2.0) < 1.0e-6 || std::abs (note.getEndBeat() - 2.0) < 1.0e-6) ? 1 : 0;

        CHECK (cutAtTwo == 4);

        f.project->undo();
        CHECK (f.notes().size() == 3);
    }

    TEST_CASE ("Ctrl+drag selects with a rectangle, Ctrl+Shift adds, Ctrl+click on empty space deselects")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);
        f.addNote (62, 1.0, 1.0);
        f.addNote (70, 3.0, 1.0);

        f.drag (f.at (0.5, 63), f.at (1.5, 59), ctrl);
        CHECK (f.model.getNumSelected() == 2);
        CHECK (f.notes().size() == 3);     // nothing was drawn

        f.drag (f.at (2.9, 71), f.at (3.5, 69), ctrl | shift);
        CHECK (f.model.getNumSelected() == 3);

        f.click (f.at (6.0, 40), ctrl);
        CHECK (f.model.getNumSelected() == 0);
    }

    TEST_CASE ("the Select tool: click selects one note, drag moves the selection")
    {
        PianoRollFixture f;
        auto* a = f.addNote (60, 0.0, 1.0);
        auto* b = f.addNote (62, 1.0, 1.0);
        f.model.setTool (Tool::select);
        f.model.setSelection ({ a, b });

        f.click (f.at (1.5, 62));
        CHECK (f.model.getNumSelected() == 1);
        CHECK (f.model.isSelected (*b));

        f.click (f.at (0.5, 60), shift);
        CHECK (f.model.getNumSelected() == 2);

        f.drag (f.at (0.5, 60), f.at (1.5, 60));
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.0));
        CHECK (f.notes()[1].startBeat == doctest::Approx (2.0));

        f.drag (f.at (8.0, 40), f.at (8.5, 39));    // empty rectangle
        CHECK (f.model.getNumSelected() == 0);
    }

    TEST_CASE ("a double click on a note asks for its properties")
    {
        PianoRollFixture f;
        auto* a = f.addNote (60, 0.0, 1.0);
        te::MidiNote* asked = nullptr;
        f.gestures.onNoteDoubleClicked = [&] (te::MidiNote& n) { asked = &n; };

        f.gestures.down (PianoRollFixture::pointer (f.at (0.5, 60), juce::ModifierKeys::leftButtonModifier, 2));
        f.gestures.up (PianoRollFixture::pointer (f.at (0.5, 60)));
        CHECK (asked == a);
    }

    TEST_CASE ("drawing past the end of the clip makes it longer by whole bars; undo shrinks it back")
    {
        PianoRollFixture f;
        CHECK (f.model.getClipEndBeat() == doctest::Approx (16.0));

        f.click (f.at (17.0, 60));
        CHECK (f.model.getClipEndBeat() == doctest::Approx (20.0));

        f.project->undo();
        CHECK (f.model.getClipEndBeat() == doctest::Approx (16.0));
        CHECK (f.notes().empty());
    }

    TEST_CASE ("notes can't be moved before the start of the clip or out of the pitch range")
    {
        PianoRollFixture f;
        f.addNote (126, 0.5, 1.0);
        f.view.topRow = 0.0f;

        f.drag (f.at (0.7, 126), f.at (-3.0, 140));
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (0.0));
        CHECK (f.notes()[0].pitch == 127);
    }
}

TEST_SUITE ("piano roll: commands")
{
    TEST_CASE ("Delete removes the selection, or every note when nothing is selected")
    {
        PianoRollFixture f;
        auto* a = f.addNote (60, 0.0, 1.0);
        f.addNote (62, 1.0, 1.0);
        f.addNote (64, 2.0, 1.0);

        f.model.setSelection ({ a });
        f.model.deleteSelectedOrAll();
        CHECK (f.notes().size() == 2);
        CHECK (f.model.getNumSelected() == 0);

        f.model.deleteSelectedOrAll();
        CHECK (f.notes().empty());

        f.project->undo();
        CHECK (f.notes().size() == 2);
    }

    TEST_CASE ("copy and paste at the first whole bar on screen, keeping the position in the bar")
    {
        PianoRollFixture f;
        auto* a = f.addNote (60, 1.0, 0.5);
        auto* b = f.addNote (64, 2.5, 0.5);
        f.addNote (67, 6.0, 0.5);
        f.model.setSelection ({ a, b });

        f.model.copy();
        CHECK (f.model.canPaste());

        const auto at = f.model.getPastePosition (5.0);
        CHECK (at == doctest::Approx (8.0));
        f.model.paste (at);

        const auto n = f.notes();
        REQUIRE (n.size() == 5);
        CHECK (n[3].startBeat == doctest::Approx (9.0));
        CHECK (n[4].startBeat == doctest::Approx (10.5));
        CHECK (f.model.getNumSelected() == 2);   // the pasted notes

        f.project->undo();
        CHECK (f.notes().size() == 3);
    }

    TEST_CASE ("cut removes notes and paste brings them back")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);
        f.addNote (62, 1.0, 1.0);

        f.model.cut();
        CHECK (f.notes().empty());

        f.model.paste (f.model.getPastePosition (0.0));
        const auto n = f.notes();
        REQUIRE (n.size() == 2);
        CHECK (n[0].startBeat == doctest::Approx (0.0));
        CHECK (n[1].pitch == 62);
    }

    TEST_CASE ("duplicate puts the copy right after the notes and selects it")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 0.5);
        f.addNote (62, 1.0, 0.75);

        f.model.duplicate();
        auto n = f.notes();
        REQUIRE (n.size() == 4);
        CHECK (n[2].startBeat == doctest::Approx (2.0));
        CHECK (n[3].startBeat == doctest::Approx (3.0));
        CHECK (f.model.getNumSelected() == 2);

        // Again: only the selection is duplicated.
        f.model.duplicate();
        n = f.notes();
        REQUIRE (n.size() == 6);
        CHECK (n[4].startBeat == doctest::Approx (4.0));
    }

    TEST_CASE ("duplicate with a time selection repeats by the selection's length")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 0.5);
        f.addNote (62, 3.0, 0.5);
        f.addNote (64, 5.0, 0.5);    // outside the time selection

        f.model.setTimeSelection ({ 0.0, 4.0 });
        CHECK (f.model.getNumSelected() == 2);

        f.model.duplicate();
        const auto n = f.notes();
        REQUIRE (n.size() == 5);
        CHECK (n[1].startBeat == doctest::Approx (3.0));
        CHECK (n[2].startBeat == doctest::Approx (4.0));
        CHECK (n[4].startBeat == doctest::Approx (7.0));
        REQUIRE (f.model.getTimeSelection().has_value());
        CHECK (f.model.getTimeSelection()->start == doctest::Approx (4.0));
    }

    TEST_CASE ("quantize and quantize start times use the snap")
    {
        PianoRollFixture f;
        f.addNote (60, 1.1, 0.9);

        f.model.quantize (true);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.0));
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (0.9));

        f.project->undo();
        f.model.quantize (false);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.0));
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (1.0));
    }

    TEST_CASE ("transpose and nudge work on the selection or on everything")
    {
        PianoRollFixture f;
        auto* a = f.addNote (60, 1.0, 1.0);
        f.addNote (64, 2.0, 1.0);

        f.model.transpose (12);
        CHECK (f.notes()[0].pitch == 72);
        CHECK (f.notes()[1].pitch == 76);

        f.model.setSelection ({ a });
        f.model.transpose (-1);
        f.model.nudge (1);
        CHECK (f.notes()[0].pitch == 71);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.25));
        CHECK (f.notes()[1].pitch == 76);

        // Can't go below the start of the clip.
        f.model.nudge (-100);
        CHECK (f.notes()[0].startBeat == doctest::Approx (0.0));
    }

    TEST_CASE ("select all / deselect all and the time selection")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);
        f.addNote (62, 4.0, 1.0);

        f.model.selectAll();
        CHECK (f.model.getNumSelected() == 2);

        f.model.setTimeSelection ({ 3.0, 8.0 });
        CHECK (f.model.getNumSelected() == 1);

        f.model.deselectAll();
        CHECK (f.model.getNumSelected() == 0);
        CHECK_FALSE (f.model.getTimeSelection().has_value());
    }

    TEST_CASE ("tools, snap and the lower panel's value can be switched")
    {
        PianoRollFixture f;

        for (auto tool : getAllTools())
        {
            f.model.setTool (tool);
            CHECK (f.model.getTool() == tool);
            CHECK (getName (tool).isNotEmpty());
            CHECK (getShortcutKey (tool) != 0);
        }

        f.model.setLaneProperty (core::NoteProperty::velocity);
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneProperty() == core::NoteProperty::release);
        f.model.cycleLaneProperty (-2);     // back past the first: the controllers come last
        CHECK (f.model.getLaneController() == core::controllers::Controller::sustain);
        f.model.cycleLaneProperty (-3);
        CHECK_FALSE (f.model.getLaneController().has_value());
        CHECK (f.model.getLaneProperty() == core::NoteProperty::finePitch);
    }
}

TEST_SUITE ("piano roll: lower panel")
{
    TEST_CASE ("a swipe sets the velocity of every note it crosses, following the mouse")
    {
        PianoRollFixture f;

        for (int i = 0; i < 8; ++i)
            f.addNote (60, i * 0.5, 0.25);

        f.model.beginGesture ("swipe");
        f.model.setLaneValues (0.0, 0.0f, 3.5, 1.0f, 0.01);

        const auto n = f.notes();
        CHECK (n.front().velocity == 0);
        CHECK (n.back().velocity == 127);

        for (size_t i = 1; i < n.size(); ++i)
            CHECK (n[i].velocity > n[i - 1].velocity);

        f.project->undo();
        CHECK (f.notes()[3].velocity == 100);
    }

    TEST_CASE ("with a selection only the selected note of a chord changes")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);
        auto* middle = f.addNote (64, 0.0, 1.0);
        f.addNote (67, 0.0, 1.0);
        f.model.setSelection ({ middle });

        f.model.beginGesture ("swipe");
        f.model.setLaneValues (0.0, 0.25f, 0.0, 0.25f, 0.05);

        for (const auto& d : f.model.getNotes())
            CHECK (d.values.velocity == (d.note == middle ? 32 : 100));
    }

    TEST_CASE ("the stem nearest the mouse follows it even when the mouse is a little to the side")
    {
        PianoRollFixture f;
        f.addNote (60, 0.0, 1.0);
        f.addNote (64, 0.0, 1.0);     // a chord: both notes share the stem
        f.addNote (60, 1.0, 1.0);

        f.model.beginGesture ("drag");
        f.model.setLaneValues (0.05, 0.5f, 0.1, 0.2f, 0.15);

        const auto n = f.notes();
        CHECK (n[0].velocity == 25);
        CHECK (n[1].velocity == 25);
        CHECK (n[2].velocity == 100);

        // Too far from any stem: nothing changes.
        f.model.setLaneValues (0.5, 0.9f, 0.5, 0.9f, 0.15);
        CHECK (f.notes()[0].velocity == 25);
        CHECK (f.notes()[2].velocity == 100);
        CHECK (f.model.findLaneNoteNear (0.5, 0.15) == nullptr);
        CHECK (f.model.findLaneNoteNear (0.9, 0.15) != nullptr);
    }

    TEST_CASE ("Alt+wheel changes a value; quick turns are one undo step")
    {
        PianoRollFixture f;
        auto* a = f.addNote (60, 0.0, 1.0);
        f.model.setLaneProperty (core::NoteProperty::pan);

        f.model.nudgeProperty (*a, 0.1f);
        f.model.nudgeProperty (*a, 0.1f);
        CHECK (f.notes()[0].pan == doctest::Approx (0.4f).epsilon (0.01));

        f.project->undo();
        CHECK (f.notes()[0].pan == doctest::Approx (0.0f));
    }
}

TEST_SUITE ("piano roll: other tracks and the clip")
{
    TEST_CASE ("ghost notes show the other tracks' notes in song time, cut to their clips")
    {
        PianoRollFixture f;
        auto* track2 = f.project->addTrack ("Bass");
        auto* clip2 = f.project->addMidiClip (*track2, 4.0, 4.0);
        REQUIRE (clip2 != nullptr);

        core::NoteEditor editor2 (*clip2);
        Note n;
        n.pitch = 40;
        n.startBeat = 1.0;     // song beat 5
        n.lengthBeats = 1.0;
        editor2.add (n);
        n.startBeat = 3.5;     // runs past the clip end: cut at song beat 8
        editor2.add (n);

        const auto& ghosts = f.model.getGhostNotes();
        REQUIRE (ghosts.size() == 2);
        CHECK (ghosts[0].startBeat == doctest::Approx (5.0));
        CHECK (ghosts[1].startBeat == doctest::Approx (7.5));
        CHECK (ghosts[1].lengthBeats == doctest::Approx (0.5));

        const auto [first, last] = f.model.getGhostNotesInRange (6.0, 7.0);
        CHECK (last - first <= 2);

        // The edited clip's own notes are not ghosts.
        f.addNote (60, 0.0, 1.0);
        CHECK (f.model.getGhostNotes().size() == 2);
    }

    TEST_CASE ("when the clip disappears (undo of its track) the piano roll shows nothing and edits nothing")
    {
        auto project = core::Project::createEmpty (test::session());
        PianoRollModel model (*project);

        project->beginGesture ("Add track");
        auto* track = project->addTrack ("Lead");
        auto* clip = core::clips::getOrAddMainClip (*project, *track, 16.0);
        model.setClip (clip);

        Note n;
        model.beginGesture ("note");
        model.addNote (n);
        CHECK (model.getNotes().size() == 1);

        project->undo();   // the note
        project->undo();   // the track and its clip
        CHECK (model.getNotes().empty());
        CHECK (model.addNote (n) == nullptr);
    }

    TEST_CASE ("editing works on a clip that doesn't start at the beginning of the song")
    {
        PianoRollFixture f;
        auto* later = f.project->addMidiClip (*f.track, 32.0, 8.0);
        f.model.setClip (later);

        CHECK (f.model.getContentStartBeat() == doctest::Approx (32.0));
        f.click (f.at (33.1, 60));
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (33.0));                         // song beats
        CHECK (later->getSequence().getNotes()[0]->getStartBeat().inBeats() == doctest::Approx (1.0));   // stored in the clip

        f.drag (f.at (33.1, 60), f.at (20.0, 60));     // can't go before the clip
        CHECK (f.notes()[0].startBeat == doctest::Approx (32.0));
    }
}
