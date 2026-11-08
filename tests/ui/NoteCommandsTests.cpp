#include <doctest.h>

#include "core/TestProject.h"
#include "ui/ComputerKeyboard.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
using namespace harmonia::ui::pianoroll;
using core::Note;
namespace snap = core::snap;

namespace
{
    struct CommandsFixture : test::ProjectWithClip
    {
        CommandsFixture()
        {
            model.setClip (clip);
            model.setSnap (snap::Mode::quarterBeat);
            model.setPixelsPerBeat (100.0);
        }

        te::MidiNote* add (int pitch, double start, double length, int velocity = 100)
        {
            Note n;
            n.pitch = pitch;
            n.startBeat = start;
            n.lengthBeats = length;
            n.velocity = velocity;
            model.beginGesture ("test");
            return model.addNote (n);
        }

        /** Notes in time order (then by pitch). */
        std::vector<Note> notes()
        {
            std::vector<Note> result;

            for (const auto& d : model.getNotes())
                result.push_back (d.values);

            std::stable_sort (result.begin(), result.end(), [] (const Note& a, const Note& b)
                              { return std::tie (a.startBeat, a.pitch) < std::tie (b.startBeat, b.pitch); });
            return result;
        }

        PianoRollModel model { *project };
    };
}

TEST_SUITE ("piano roll: note commands")
{
    TEST_CASE ("quick legato stretches the notes to the next ones, one undo step")
    {
        CommandsFixture f;
        f.add (60, 0.0, 0.25);
        f.add (64, 0.0, 0.25);
        f.add (62, 1.5, 0.25);
        f.add (67, 4.0, 0.5);

        f.model.legato();
        auto n = f.notes();
        CHECK (n[0].lengthBeats == doctest::Approx (1.5));
        CHECK (n[1].lengthBeats == doctest::Approx (1.5));
        CHECK (n[2].lengthBeats == doctest::Approx (2.5));
        CHECK (n[3].lengthBeats == doctest::Approx (0.5));

        f.project->undo();
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (0.25));
        CHECK (f.notes()[2].lengthBeats == doctest::Approx (0.25));
    }

    TEST_CASE ("quick chop cuts into snap steps; the pieces of selected notes stay selected")
    {
        CommandsFixture f;
        auto* longNote = f.add (60, 1.0, 1.0, 90);
        f.add (62, 4.0, 1.0);
        f.model.setSelection ({ longNote });

        f.model.chop();
        auto n = f.notes();
        REQUIRE (n.size() == 5);

        for (int i = 0; i < 4; ++i)
        {
            CHECK (n[(size_t) i].startBeat == doctest::Approx (1.0 + 0.25 * i));
            CHECK (n[(size_t) i].lengthBeats == doctest::Approx (0.25));
            CHECK (n[(size_t) i].velocity == 90);
        }

        CHECK (n[4].lengthBeats == doctest::Approx (1.0));    // not selected: left alone
        CHECK (f.model.getNumSelected() == 4);

        f.project->undo();
        CHECK (f.notes().size() == 2);
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (1.0));

        // With a coarser snap, coarser pieces.
        f.model.deselectAll();
        f.model.setSnap (snap::Mode::halfBeat);
        f.model.chop();
        CHECK (f.notes().size() == 4);
        CHECK (f.model.getNumSelected() == 0);
    }

    TEST_CASE ("glue joins what chop cut, keeping the first note's values")
    {
        CommandsFixture f;
        f.add (60, 1.0, 1.0, 90);
        f.model.chop();
        REQUIRE (f.notes().size() == 4);

        f.model.glue();
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.0));
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (1.0));
        CHECK (f.notes()[0].velocity == 90);

        f.project->undo();
        CHECK (f.notes().size() == 4);

        // Different pitches and gaps aren't glued.
        CommandsFixture g;
        g.add (60, 0.0, 1.0);
        g.add (62, 1.0, 1.0);
        g.add (60, 2.5, 1.0);
        g.model.glue();
        CHECK (g.notes().size() == 3);
    }

    TEST_CASE ("discard lengths makes the notes one snap step long")
    {
        CommandsFixture f;
        f.add (60, 0.0, 3.0);
        f.add (62, 1.0, 0.1);

        f.model.discardLengths();
        CHECK (f.notes()[0].lengthBeats == doctest::Approx (0.25));
        CHECK (f.notes()[1].lengthBeats == doctest::Approx (0.25));
    }

    TEST_CASE ("rotate wraps the notes around inside their bar, or inside the time selection")
    {
        CommandsFixture f;
        f.add (60, 0.0, 0.25);
        f.add (62, 3.75, 0.25);
        f.add (64, 4.5, 0.25);

        // Nothing selected: the notes start in bars 1-2, so the first note wraps round to the end.
        f.model.rotate (-2);
        auto n = f.notes();
        CHECK (n[0].pitch == 62);
        CHECK (n[0].startBeat == doctest::Approx (3.25));
        CHECK (n[1].pitch == 64);
        CHECK (n[1].startBeat == doctest::Approx (4.0));
        CHECK (n[2].pitch == 60);
        CHECK (n[2].startBeat == doctest::Approx (7.5));

        f.project->undo();
        CHECK (f.notes()[0].startBeat == doctest::Approx (0.0));

        // Time selection: only the first bar turns.
        f.model.setTimeSelection ({ 0.0, 4.0 });
        f.model.rotate (1);
        n = f.notes();
        CHECK (n[0].pitch == 62);
        CHECK (n[0].startBeat == doctest::Approx (0.0));
        CHECK (n[1].pitch == 60);
        CHECK (n[1].startBeat == doctest::Approx (0.25));
        CHECK (n[2].startBeat == doctest::Approx (4.5));
    }

    TEST_CASE ("Alt+arrows move by a couple of pixels, off the grid")
    {
        CommandsFixture f;
        f.add (60, 1.0, 1.0);

        f.model.nudgeFine (1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.02).epsilon (0.002));

        f.model.setPixelsPerBeat (400.0);
        f.model.nudgeFine (-1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (1.015).epsilon (0.002));

        // Not before the clip's start.
        f.model.nudgeFine (-1000);
        CHECK (f.notes()[0].startBeat >= 0.0);
    }

    TEST_CASE ("mute, unmute and invert selection")
    {
        CommandsFixture f;
        auto* a = f.add (60, 0.0, 1.0);
        f.add (62, 1.0, 1.0);

        f.model.setSelection ({ a });
        f.model.setMuted (true);
        CHECK (f.notes()[0].muted);
        CHECK_FALSE (f.notes()[1].muted);

        f.model.invertSelection();
        CHECK (f.model.getNumSelected() == 1);
        CHECK_FALSE (f.model.isSelected (*a));

        f.model.deselectAll();
        f.model.setMuted (true);
        CHECK (f.notes()[1].muted);
        f.model.setMuted (false);
        CHECK_FALSE (f.notes()[0].muted);
        CHECK_FALSE (f.notes()[1].muted);

        f.project->undo();
        CHECK (f.notes()[0].muted);
        CHECK (f.notes()[1].muted);
    }

    TEST_CASE ("insert and delete space at the time selection move every note")
    {
        CommandsFixture f;
        f.add (60, 0.0, 1.0);
        f.add (62, 1.0, 2.0);
        f.add (64, 2.0, 0.5);
        f.add (65, 4.0, 1.0);

        // Nothing without a time selection.
        f.model.insertSpace();
        f.model.deleteSpace();
        CHECK (f.notes()[3].startBeat == doctest::Approx (4.0));

        f.model.setTimeSelection ({ 2.0, 3.0 });
        f.model.insertSpace();
        auto n = f.notes();
        CHECK (n[0].startBeat == doctest::Approx (0.0));
        CHECK (n[1].startBeat == doctest::Approx (1.0));
        CHECK (n[2].startBeat == doctest::Approx (3.0));
        CHECK (n[3].startBeat == doctest::Approx (5.0));

        f.project->undo();
        f.model.setTimeSelection ({ 2.0, 3.0 });
        f.model.deleteSpace();
        n = f.notes();
        REQUIRE (n.size() == 3);
        CHECK (n[1].lengthBeats == doctest::Approx (1.0));    // lost the deleted beat
        CHECK (n[2].pitch == 65);
        CHECK (n[2].startBeat == doctest::Approx (3.0));

        f.project->undo();
        CHECK (f.notes().size() == 4);
    }

    TEST_CASE ("the keys, with the Russian layout too, and the Tools menu")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 16.0, "Clip");
        auto& model = main.getPianoRollModel();
        model.setClip (clip);
        model.setSnap (snap::Mode::quarterBeat);

        const auto add = [&] (int pitch, double start, double length)
        {
            Note n;
            n.pitch = pitch;
            n.startBeat = start;
            n.lengthBeats = length;
            model.beginGesture ("test");
            return model.addNote (n);
        };

        const auto key = [] (int code, juce::juce_wchar text, int mods)
        {
            return juce::KeyPress (code, juce::ModifierKeys (mods), text);
        };

        constexpr int ctrl = juce::ModifierKeys::ctrlModifier;
        constexpr int shift = juce::ModifierKeys::shiftModifier;
        constexpr int alt = juce::ModifierKeys::altModifier;

        add (60, 0.0, 1.0);
        add (64, 2.0, 0.5);

        const auto lengthOf = [&] (int pitch)
        {
            for (const auto& d : model.getNotes())
                if (d.values.pitch == pitch)
                    return d.values.lengthBeats;

            return -1.0;
        };

        // Ctrl+L (Д): legato.
        CHECK (main.keyPressed (key ('L', 0x0434, ctrl)));
        CHECK (lengthOf (60) == doctest::Approx (2.0));

        // Ctrl+U (Г): chop. Ctrl+G (П): glue back.
        CHECK (main.keyPressed (key ('U', 0x0433, ctrl)));
        CHECK (model.getNotes().size() == 10);
        CHECK (main.keyPressed (key ('G', 0x043f, ctrl)));
        CHECK (model.getNotes().size() == 2);

        // Alt+M (Ь) / Alt+Shift+M.
        CHECK (main.keyPressed (key ('M', 0x044c, alt)));
        CHECK (model.getNotes()[0].values.muted);
        CHECK (main.keyPressed (key ('M', 0x042c, alt | shift)));
        CHECK_FALSE (model.getNotes()[0].values.muted);

        // Shift+I (Ш): invert selection. Shift+D (В): discard lengths.
        CHECK (main.keyPressed (key ('I', 0x0428, shift)));
        CHECK (model.getNumSelected() == 2);
        CHECK (main.keyPressed (key ('D', 0x0412, shift)));
        CHECK (lengthOf (60) == doctest::Approx (0.25));

        // Ctrl+Shift+Right: rotate.
        CHECK (main.keyPressed (key (juce::KeyPress::rightKey, 0, ctrl | shift)));
        CHECK (model.getNotes()[0].values.startBeat == doctest::Approx (0.25));

        // Alt+Left: a little earlier.
        CHECK (main.keyPressed (key (juce::KeyPress::leftKey, 0, alt)));
        CHECK (model.getNotes()[0].values.startBeat < 0.25);
        CHECK (model.getNotes()[0].values.startBeat > 0.2);

        // Ctrl+Insert / Ctrl+Delete with a time selection.
        const auto before = model.getNotes()[1].values.startBeat;
        model.setTimeSelection ({ 1.0, 2.0 });
        CHECK (main.keyPressed (key (juce::KeyPress::insertKey, 0, ctrl)));
        CHECK (model.getNotes()[1].values.startBeat == doctest::Approx (before + 1.0));
        CHECK (main.keyPressed (key (juce::KeyPress::deleteKey, 0, ctrl)));
        CHECK (model.getNotes()[1].values.startBeat == doctest::Approx (before));

        // The Tools menu lists the commands with their keys.
        auto& pianoRoll = main.getPianoRoll();
        const auto menu = pianoRoll.createToolsMenu();
        juce::StringArray items;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().text.isNotEmpty() && ! it.getItem().isSectionHeader)
                items.add (it.getItem().text + " " + it.getItem().shortcutKeyDescription);

        CHECK (items.contains ("Quick legato Ctrl+L"));
        CHECK (items.contains ("Glue Ctrl+G"));
        CHECK (items.contains ("Delete space Ctrl+Delete"));
    }
}
