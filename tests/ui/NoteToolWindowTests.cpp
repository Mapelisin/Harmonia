#include <doctest.h>

#include "core/TestProject.h"
#include "ui/MainComponent.h"
#include "ui/pianoroll/NotePropertiesPanel.h"
#include "ui/pianoroll/NoteToolWindow.h"

using namespace harmonia;
using namespace harmonia::ui;
using namespace harmonia::ui::pianoroll;
using core::Note;

namespace
{
    struct ToolsFixture : test::ProjectWithClip
    {
        ToolsFixture()
        {
            model.setClip (clip);
            model.setSnap (core::snap::Mode::quarterBeat);
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

        /** A C major chord at beat 0 and a single note at beat 2. */
        void addChordAndNote()
        {
            chord = { add (60, 0.0, 1.0), add (64, 0.0, 1.0), add (67, 0.0, 1.0) };
            single = add (72, 2.0, 1.0);
        }

        std::vector<Note> notes()
        {
            std::vector<Note> result;

            for (const auto& d : model.getNotes())
                result.push_back (d.values);

            std::stable_sort (result.begin(), result.end(), [] (const Note& a, const Note& b)
                              { return std::tie (a.startBeat, a.pitch) < std::tie (b.startBeat, b.pitch); });
            return result;
        }

        /** Lets the edit's timer start a new undo step, as it does when the user pauses. */
        static void pause()
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (450);
        }

        PianoRollModel model { *project };
        juce::Array<te::MidiNote*> chord;
        te::MidiNote* single = nullptr;
    };

    int indexOf (const NoteToolWindow& window, const juce::String& name)
    {
        for (int i = 0; i < window.getNumSettings(); ++i)
            if (window.getSetting (i).name == name)
                return i;

        FAIL ("no setting " << name);
        return -1;
    }
}

TEST_SUITE ("piano roll: tool windows")
{
    TEST_CASE ("a preview replaces its last result, and everything shown is one undo step even after pauses")
    {
        ToolsFixture f;
        f.addChordAndNote();
        const auto before = f.notes();

        {
            ToolPreview preview (f.model, "Test tool", f.chord);

            for (int velocity : { 10, 20, 30 })
            {
                auto values = preview.getOriginals();

                for (auto& v : values)
                    v.velocity = velocity;

                preview.show (values);
                ToolsFixture::pause();
            }

            CHECK (f.notes()[0].velocity == 30);
        }

        f.project->undo();
        CHECK (f.notes() == before);

        f.project->redo();
        CHECK (f.notes()[0].velocity == 30);
    }

    TEST_CASE ("revert puts the notes back and keeps what could be redone")
    {
        ToolsFixture f;
        f.addChordAndNote();
        f.add (40, 3.0, 1.0);
        f.project->undo();
        REQUIRE (f.notes().size() == 4);
        const auto before = f.notes();

        ToolPreview preview (f.model, "Test tool", f.chord);
        preview.show (core::note_tools::Result { preview.getOriginals(), { 0, 1, 2 }, { Note {} } });
        CHECK (f.notes().size() == 2);
        CHECK_FALSE (f.project->canRedo());

        preview.revert();
        CHECK (f.notes() == before);
        CHECK (f.project->canRedo());
    }

    TEST_CASE ("strum works live on the selected chord; Cancel puts it back")
    {
        ToolsFixture f;
        f.addChordAndNote();
        f.model.setSelection (f.chord);
        const auto before = f.notes();

        auto window = NoteToolWindow::create (f.model, NoteTool::strum);
        REQUIRE (window != nullptr);

        // Opens with its default settings already applied (40 ms at 120 bpm = 0.08 beats).
        auto n = f.notes();
        CHECK (n[0].startBeat == doctest::Approx (0.0));
        CHECK (n[1].startBeat == doctest::Approx (0.04).epsilon (0.01));
        CHECK (n[2].startBeat == doctest::Approx (0.08).epsilon (0.01));
        CHECK (n[3].startBeat == doctest::Approx (2.0));

        window->setValue (indexOf (*window, "Time"), 250.0);
        CHECK (f.notes()[2].startBeat == doctest::Approx (0.5).epsilon (0.01));
        ToolsFixture::pause();
        window->setValue (indexOf (*window, "Direction"), 1.0);
        CHECK (f.notes()[0].pitch == 67);
        CHECK (f.model.getNumSelected() == 3);

        window->cancel();
        window.reset();
        CHECK (f.notes() == before);
    }

    TEST_CASE ("Accept keeps the result as one undo step; Reset goes back to the defaults")
    {
        ToolsFixture f;
        f.addChordAndNote();
        const auto before = f.notes();

        auto window = NoteToolWindow::create (f.model, NoteTool::humanize);
        REQUIRE (window != nullptr);
        const auto humanized = f.notes();
        CHECK_FALSE (humanized == before);     // all four notes, nothing being selected

        window->setValue (indexOf (*window, "Velocity"), 64.0);
        ToolsFixture::pause();
        window->setValue (indexOf (*window, "Variation"), 7.0);
        CHECK_FALSE (f.notes() == humanized);

        window->reset();
        CHECK (f.notes() == humanized);

        window->setValue (indexOf (*window, "Timing"), 0.0);
        window->setValue (indexOf (*window, "Velocity"), 0.0);
        window->setValue (indexOf (*window, "Velocity"), 30.0);
        const auto accepted = f.notes();
        window->accept();
        window.reset();
        CHECK (f.notes() == accepted);

        f.project->undo();
        CHECK (f.notes() == before);
    }

    TEST_CASE ("arpeggiate replaces the chord with its notes one after another, selected")
    {
        ToolsFixture f;
        f.addChordAndNote();
        f.model.selectAll();

        auto window = NoteToolWindow::create (f.model, NoteTool::arpeggiate);
        REQUIRE (window != nullptr);

        // 1/16 notes over the one-beat chord, the single note as it was.
        auto n = f.notes();
        REQUIRE (n.size() == 5);
        CHECK (n[0].pitch == 60);
        CHECK (n[1].pitch == 64);
        CHECK (n[2].pitch == 67);
        CHECK (n[3].pitch == 60);
        CHECK (n[3].startBeat == doctest::Approx (0.75));
        CHECK (n[4].pitch == 72);
        CHECK (f.model.getNumSelected() == 5);

        window->setValue (indexOf (*window, "Octaves"), 2.0);
        window->setValue (indexOf (*window, "Step"), 5.0);     // 1/32
        CHECK (f.notes().size() == 9);
        CHECK (f.notes()[3].pitch == 72);

        window.reset();
        f.project->undo();
        CHECK (f.notes().size() == 4);
    }

    TEST_CASE ("scale levels changes the value shown in the lower panel")
    {
        ToolsFixture f;
        f.add (60, 0.0, 1.0, 100);
        f.add (62, 1.0, 1.0, 60);

        auto window = NoteToolWindow::create (f.model, NoteTool::scaleLevels);
        REQUIRE (window != nullptr);
        CHECK (f.notes()[0].velocity == 100);     // no change at first

        window->setValue (indexOf (*window, "Multiply"), 50.0);
        CHECK (f.notes()[0].velocity == 50);
        CHECK (f.notes()[1].velocity == 30);
    }

    TEST_CASE ("no notes, no window (but the chord progression and the drum pattern write into an empty clip)")
    {
        ToolsFixture f;

        for (auto tool : getAllNoteTools())
            CHECK ((NoteToolWindow::create (f.model, tool) == nullptr)
                   == (tool != NoteTool::chordProgression && tool != NoteTool::drumPattern));
    }

    TEST_CASE ("the drum pattern writes a groove live, as long as the clip, as one undo step")
    {
        ToolsFixture f;

        {
            auto window = NoteToolWindow::create (f.model, NoteTool::drumPattern);
            REQUIRE (window != nullptr);
            CHECK (window->getToolName() == "Drum pattern");
            CHECK (window->getValue (indexOf (*window, "Bars")) == 4.0);       // the clip's four bars

            // Rock: three bars of 13 hits (the first with a crash for its hat), the fourth ending in a fill.
            auto n = f.notes();
            CHECK (n.size() == 3 * 13 + 6 + 10);
            CHECK (f.model.getNumSelected() == (int) n.size());
            REQUIRE (! n.empty());
            CHECK (n.front().startBeat == doctest::Approx (0.0));
            CHECK (n.back().startBeat < 16.0);

            // House: the kick on every beat.
            window->setValue (indexOf (*window, "Style"), 2.0);
            n = f.notes();
            CHECK (std::count_if (n.begin(), n.end(), [] (const Note& note) { return note.pitch == 36; }) == 16);

            window->setValue (indexOf (*window, "Bars"), 8.0);
            CHECK (f.model.getClipEndBeat() >= 32.0);                   // the clip grew to hold them
            window->accept();
        }

        CHECK_FALSE (f.notes().empty());
        f.project->undo();
        CHECK (f.notes().empty());
        CHECK (f.model.getClipEndBeat() == doctest::Approx (16.0));

        // From the start of a time selection, as long as it.
        f.model.setTimeSelection ({ 4.0, 12.0 });

        {
            auto window = NoteToolWindow::create (f.model, NoteTool::drumPattern);
            REQUIRE (window != nullptr);
            CHECK (window->getValue (indexOf (*window, "Bars")) == 2.0);

            const auto n = f.notes();
            REQUIRE (! n.empty());
            CHECK (n.front().startBeat == doctest::Approx (4.0));
            CHECK (n.back().startBeat < 12.0);
            window->cancel();
        }

        CHECK (f.notes().empty());
    }

    TEST_CASE ("the chord progression writes chords live, selected, as one undo step; Cancel takes them away")
    {
        ToolsFixture f;
        f.add (40, 0.0, 1.0);       // something already there stays
        f.model.deselectAll();

        {
            auto window = NoteToolWindow::create (f.model, NoteTool::chordProgression);
            REQUIRE (window != nullptr);
            CHECK (window->getToolName() == "Chord progression");

            // I - V - vi - IV in C, a bar each, close together.
            auto n = f.notes();
            REQUIRE (n.size() == 13);
            CHECK (n[1].pitch == 60);
            CHECK (n[1].lengthBeats == doctest::Approx (4.0));
            CHECK (f.model.getNumSelected() == 12);

            window->setValue (indexOf (*window, "Rhythm"), 1.0);       // quarter notes
            ToolsFixture::pause();
            window->setValue (indexOf (*window, "Bass"), 1.0);
            ToolsFixture::pause();
            window->setValue (indexOf (*window, "Repeat"), 2.0);
            CHECK (f.notes().size() == 1 + 2 * (48 + 4));
            CHECK (f.model.getClipEndBeat() >= 32.0);                   // the clip grew to hold them
            window->accept();
        }

        f.project->undo();
        CHECK (f.notes().size() == 1);
        CHECK (f.model.getClipEndBeat() == doctest::Approx (16.0));

        // In the song's key, from the start of the time selection.
        f.model.setScale ({ 9, core::scales::ScaleType::minor });
        f.model.setTimeSelection ({ 4.0, 8.0 });

        {
            auto window = NoteToolWindow::create (f.model, NoteTool::chordProgression);
            REQUIRE (window != nullptr);
            CHECK (window->getValue (indexOf (*window, "Key")) == 9.0);
            CHECK (window->getValue (indexOf (*window, "Mode")) == 1.0);

            auto n = f.notes();
            REQUIRE (n.size() == 13);
            CHECK (n[1].startBeat == doctest::Approx (4.0));
            CHECK (n[1].pitch == 57);       // A minor, from A3
            window->cancel();
        }

        CHECK (f.notes().size() == 1);
    }

    TEST_CASE ("the note properties panel: every change is one undo step, even after pauses")
    {
        ToolsFixture f;
        f.addChordAndNote();
        f.model.setSelection (f.chord);
        const auto before = f.notes();

        {
            NotePropertiesPanel panel (f.model, *f.chord[0]);
            panel.setRowValue (0, 50.0);    // velocity
            ToolsFixture::pause();
            panel.setRowValue (2, -50.0);   // pan
            ToolsFixture::pause();
            panel.setRowValue (0, 40.0);
        }

        auto n = f.notes();
        CHECK (n[0].velocity == 40);
        CHECK (n[2].velocity == 40);
        CHECK (n[1].pan == doctest::Approx (-0.5));
        CHECK (n[3].velocity == 100);        // not selected

        f.project->undo();
        CHECK (f.notes() == before);
    }

    TEST_CASE ("Alt+S, Alt+A, Alt+R and Alt+X open the tools (any layout); the Tools menu lists them")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 16.0, "Clip");
        auto& model = main.getPianoRollModel();
        model.setClip (clip);

        Note n;
        model.beginGesture ("test");
        model.addNote (n);

        auto& pianoRoll = main.getPianoRoll();
        std::unique_ptr<NoteToolWindow> opened;
        pianoRoll.onShowNoteTool = [&] (std::unique_ptr<NoteToolWindow> window) { opened = std::move (window); };

        // S, Ы (for S), Ф (for A), К (for R), Ч (for X), З (for P) with the Russian layout.
        for (const auto& [code, text, name] : { std::tuple { 'S', 0x044b, "Strum" }, std::tuple { 'A', 0x0444, "Arpeggiate" },
                                                std::tuple { 'R', 0x043a, "Humanize" }, std::tuple { 'X', 0x0447, "Scale levels" },
                                                std::tuple { 'P', 0x0437, "Chord progression" },
                                                std::tuple { 'D', 0x0432, "Drum pattern" } })
        {
            CHECK (main.keyPressed (juce::KeyPress (code, juce::ModifierKeys::altModifier, (juce::juce_wchar) text)));
            REQUIRE (opened != nullptr);
            CHECK (opened->getToolName() == name);
            opened->cancel();
            opened.reset();
        }

        juce::StringArray items;

        const auto menu = pianoRoll.createToolsMenu();

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            items.add (it.getItem().text + " " + it.getItem().shortcutKeyDescription);

        CHECK (items.contains ("Strum... Alt+S"));
        CHECK (items.contains ("Arpeggiate... Alt+A"));
        CHECK (items.contains ("Humanize... Alt+R"));
        CHECK (items.contains ("Scale levels... Alt+X"));
        CHECK (items.contains ("Chord progression... Alt+P"));
        CHECK (items.contains ("Drum pattern... Alt+D"));
    }

    TEST_CASE ("the Strum window over the piano roll draws")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 16.0, "Chords");
        auto& model = main.getPianoRollModel();
        model.setClip (clip);
        model.beginGesture ("test");

        // C, Am, F, G.
        const std::vector<std::vector<int>> progression { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };

        for (size_t bar = 0; bar < progression.size(); ++bar)
        {
            for (int beat = 0; beat < 4; beat += 2)
            {
                for (auto pitch : progression[bar])
                {
                    Note n;
                    n.pitch = pitch;
                    n.startBeat = (double) bar * 4.0 + beat;
                    n.lengthBeats = 1.5;
                    model.addNote (n);
                }
            }
        }

        auto& pianoRoll = main.getPianoRoll();
        pianoRoll.showClip();

        std::unique_ptr<NoteToolWindow> window;
        pianoRoll.onShowNoteTool = [&] (std::unique_ptr<NoteToolWindow> w) { window = std::move (w); };
        pianoRoll.openNoteTool (NoteTool::strum);
        REQUIRE (window != nullptr);

        // Where the call-out box would put it, under the toolbar.
        pianoRoll.addAndMakeVisible (*window);
        window->setTopLeftPosition (560, 40);

        window->setValue (indexOf (*window, "Time"), 150.0);
        window->setValue (indexOf (*window, "Direction"), 2.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-strum.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        pianoRoll.removeChildComponent (window.get());
        window.reset();
    }

    TEST_CASE ("the Drum pattern window over a drum track draws")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto& track = *project->getTracks().getFirst();
        track.setName ("Drums");        // shows drum names
        auto* clip = project->addMidiClip (track, 0.0, 16.0, "Beat");
        auto& model = main.getPianoRollModel();
        model.setClip (clip);

        auto& pianoRoll = main.getPianoRoll();
        pianoRoll.showClip();

        std::unique_ptr<NoteToolWindow> window;
        pianoRoll.onShowNoteTool = [&] (std::unique_ptr<NoteToolWindow> w) { window = std::move (w); };
        CHECK (main.keyPressed (juce::KeyPress ('D', juce::ModifierKeys::altModifier, 0)));
        REQUIRE (window != nullptr);

        pianoRoll.addAndMakeVisible (*window);
        window->setTopLeftPosition (560, 40);
        window->setValue (indexOf (*window, "Style"), 5.0);         // trap
        window->setValue (indexOf (*window, "Bars"), 2.0);
        pianoRoll.showClip();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        CHECK (! model.getNotes().empty());

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-drum-pattern.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        pianoRoll.removeChildComponent (window.get());
        window.reset();
    }

    TEST_CASE ("the Chord progression window over an empty clip draws")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 16.0, "Chords");
        auto& model = main.getPianoRollModel();
        model.setClip (clip);
        model.setScale ({ 9, core::scales::ScaleType::minor });

        auto& pianoRoll = main.getPianoRoll();
        pianoRoll.showClip();

        std::unique_ptr<NoteToolWindow> window;
        pianoRoll.onShowNoteTool = [&] (std::unique_ptr<NoteToolWindow> w) { window = std::move (w); };
        CHECK (main.keyPressed (juce::KeyPress ('P', juce::ModifierKeys::altModifier, 0)));
        REQUIRE (window != nullptr);

        pianoRoll.addAndMakeVisible (*window);
        window->setTopLeftPosition (560, 40);
        window->setValue (indexOf (*window, "Rhythm"), 3.0);        // offbeats
        window->setValue (indexOf (*window, "Bass"), 1.0);
        pianoRoll.showClip();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        CHECK (model.getNotes().size() == 4 * (4 * 3 + 1));

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-chords.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        pianoRoll.removeChildComponent (window.get());
        window.reset();
    }
}
