#include <doctest.h>

#include "core/TestProject.h"
#include "ui/MainComponent.h"
#include "ui/pianoroll/GridGestures.h"

using namespace harmonia;
using namespace harmonia::ui;
using namespace harmonia::ui::pianoroll;
using core::Note;
using core::scales::Scale;
using core::scales::ScaleType;
using core::chords::ChordType;

namespace
{
    struct RecordingAuditioner final : Auditioner
    {
        void noteOn (int pitch, int) override   { sounding.push_back (pitch); }
        void noteOff (int pitch) override       { std::erase (sounding, pitch); }

        std::vector<int> sounding;
    };

    /** A clip in the piano roll, 100 pixels per beat, 10 pixels per row, in C major. */
    struct ScaleFixture : test::ProjectWithClip
    {
        ScaleFixture()
        {
            model.setClip (clip);
            model.setAuditioner (&auditioner);
            model.setSnap (core::snap::Mode::quarterBeat);
            model.setScale ({ 0, ScaleType::major });
            model.setSnapToScale (true);

            view.width = 1600.0f;
            view.height = 900.0f;
            view.pixelsPerBeat = 100.0;
            view.rowHeight = 10.0f;
            view.topRow = 40.0f;
            model.setPixelsPerBeat (view.pixelsPerBeat);
        }

        ~ScaleFixture()
        {
            model.setAuditioner (nullptr);
        }

        juce::Point<float> at (double beat, int pitch) const
        {
            return { view.beatToX (beat), view.pitchToY (pitch) + view.rowHeight * 0.5f };
        }

        void drag (juce::Point<float> from, juce::Point<float> to, int extraFlags = 0)
        {
            const auto button = juce::ModifierKeys::leftButtonModifier;
            gestures.down ({ from, juce::ModifierKeys (button | extraFlags), 1 });

            for (int i = 1; i <= 4; ++i)
                gestures.drag ({ from + (to - from) * ((float) i / 4.0f), juce::ModifierKeys (button | extraFlags), 1 });

            gestures.up ({ to, juce::ModifierKeys (extraFlags), 1 });
        }

        void click (juce::Point<float> p)       { drag (p, p); }

        te::MidiNote* add (int pitch, double start, double length = 1.0)
        {
            Note n;
            n.pitch = pitch;
            n.startBeat = start;
            n.lengthBeats = length;
            model.beginGesture ("test");
            return model.addNote (n);
        }

        std::vector<int> pitches()
        {
            std::vector<std::pair<double, int>> sorted;

            for (const auto& d : model.getNotes())
                sorted.emplace_back (d.values.startBeat, d.values.pitch);

            std::sort (sorted.begin(), sorted.end());
            std::vector<int> result;

            for (const auto& s : sorted)
                result.push_back (s.second);

            return result;
        }

        RecordingAuditioner auditioner;
        PianoRollModel model { *project };
        View view;
        GridGestures gestures { model, view };
    };
}

TEST_SUITE ("piano roll: scale and chords")
{
    TEST_CASE ("snapping to the scale: clicked and painted notes land on it")
    {
        ScaleFixture f;
        f.click (f.at (1.0, 61));       // C#: onto C
        f.click (f.at (2.0, 66));       // F#: onto F
        CHECK (f.pitches() == std::vector<int> { 60, 65 });

        f.model.setTool (Tool::paint);
        f.drag (f.at (3.0, 70), f.at (3.9, 70));     // A#: onto A
        for (auto p : f.pitches())
            CHECK (f.model.getScale().contains (p));

        // Not snapping: any note.
        f.model.setSnapToScale (false);
        f.model.setTool (Tool::draw);
        f.click (f.at (5.0, 61));
        CHECK (f.pitches().back() == 61);
    }

    TEST_CASE ("snapping to the scale: dragged notes move by scale degrees, a chord stays in the scale")
    {
        ScaleFixture f;
        f.add (60, 1.0);
        f.add (64, 1.0);
        f.add (67, 1.0);
        f.model.selectAll();

        // Two rows up from C is D: every note one degree up, C E G -> D F A.
        f.drag (f.at (1.5, 60), f.at (1.5, 62));
        CHECK (f.pitches() == std::vector<int> { 62, 65, 69 });

        // One row up from D is D#, nearer to D: nothing moves.
        f.drag (f.at (1.5, 62), f.at (1.5, 63));
        CHECK (f.pitches() == std::vector<int> { 62, 65, 69 });

        // Shift+Up / Down: a degree at a time.
        f.model.transpose (1);
        CHECK (f.pitches() == std::vector<int> { 64, 67, 71 });
        f.model.transpose (-2);
        CHECK (f.pitches() == std::vector<int> { 62, 65, 69 });
        f.project->undo();
        CHECK (f.pitches() == std::vector<int> { 64, 67, 71 });

        // An octave is still an octave.
        f.model.transpose (12);
        CHECK (f.pitches() == std::vector<int> { 76, 79, 83 });
    }

    TEST_CASE ("fit to scale moves the notes to the nearest note of the scale, one undo step")
    {
        ScaleFixture f;
        f.model.setSnapToScale (false);
        f.add (61, 0.0);
        f.add (63, 1.0);
        f.add (64, 2.0);

        f.model.fitToScale();
        CHECK (f.pitches() == std::vector<int> { 60, 62, 64 });

        f.project->undo();
        CHECK (f.pitches() == std::vector<int> { 61, 63, 64 });

        // No scale: nothing to fit to.
        f.model.setScale ({});
        f.model.fitToScale();
        CHECK (f.pitches() == std::vector<int> { 61, 63, 64 });
    }

    TEST_CASE ("the chord stamp puts a chord down with one click and moves it whole, heard as a chord")
    {
        ScaleFixture f;
        f.model.setSnapToScale (false);
        f.model.setChordStamp (ChordType::minor7);

        f.gestures.down ({ f.at (1.0, 60), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1 });
        auto sounding = f.auditioner.sounding;
        std::sort (sounding.begin(), sounding.end());
        CHECK (sounding == std::vector<int> { 60, 63, 67, 70 });

        f.gestures.drag ({ f.at (2.0, 62), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1 });
        f.gestures.up ({ f.at (2.0, 62), {}, 1 });
        CHECK (f.auditioner.sounding.empty());

        CHECK (f.pitches() == std::vector<int> { 62, 65, 69, 72 });

        for (const auto& d : f.model.getNotes())
            CHECK (d.values.startBeat == doctest::Approx (2.0));

        f.project->undo();
        CHECK (f.pitches().empty());

        // Scale chords: in C major a click on D gives D minor, on G a G7 for "Scale chord 7".
        f.model.setChordStamp (ChordType::scaleTriad);
        f.click (f.at (1.0, 62));
        CHECK (f.pitches() == std::vector<int> { 62, 65, 69 });

        f.model.setChordStamp (ChordType::scaleSeventh);
        f.click (f.at (3.0, 67));
        CHECK (f.pitches() == std::vector<int> { 62, 65, 69, 67, 71, 74, 77 });

        // Shift+drag on empty space: a chord as long as the drag.
        f.model.setChordStamp (ChordType::major);
        f.drag (f.at (5.0, 48), f.at (7.0, 48), juce::ModifierKeys::shiftModifier);
        int longOnes = 0;

        for (const auto& d : f.model.getNotes())
            if (d.values.startBeat == doctest::Approx (5.0) && d.values.lengthBeats == doctest::Approx (2.0))
                ++longOnes;

        CHECK (longOnes == 3);
    }

    TEST_CASE ("Detect from the notes sets the key the notes (selected, or all) are in")
    {
        ScaleFixture f;

        // An A minor tune with its G sharp.
        double at = 0.0;

        for (const auto& [pitch, length] : { std::pair { 57, 2.0 }, { 60, 1.0 }, { 64, 1.0 }, { 69, 2.0 }, { 71, 1.0 },
                                             { 68, 1.0 }, { 69, 2.0 }, { 64, 1.0 }, { 62, 1.0 }, { 60, 1.0 } })
        {
            f.add (pitch, at, length);
            at += length;
        }

        f.model.deselectAll();
        CHECK (f.model.detectScale());
        CHECK (f.model.getScale() == Scale { 9, ScaleType::minor });

        f.project->undo();
        CHECK (f.model.getScale() == Scale { 0, ScaleType::major });

        // Only the selected notes count: a G major chord with F sharp.
        f.model.deselectAll();
        f.model.setSelection ({ f.add (67, 20.0, 2.0), f.add (71, 20.0, 2.0), f.add (74, 20.0, 2.0), f.add (66, 22.0, 1.0),
                                f.add (72, 23.0, 1.0) });
        CHECK (f.model.detectScale());
        CHECK (f.model.getScale() == Scale { 7, ScaleType::major });
    }

    TEST_CASE ("the scale is an undo step and marks the project changed")
    {
        ScaleFixture f;
        f.project->clearUndoHistory();
        CHECK_FALSE (f.project->hasUnsavedChanges());

        f.model.setScale ({ 9, ScaleType::minor });
        CHECK (f.project->hasUnsavedChanges());
        CHECK (f.model.getScale() == Scale { 9, ScaleType::minor });

        f.project->undo();
        CHECK (f.model.getScale() == Scale { 0, ScaleType::major });
    }

    TEST_CASE ("the Scale and Chord menus, and the snapshot with the scale lit")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 16.0, "Melody");
        auto& model = main.getPianoRollModel();
        model.setClip (clip);

        auto& pianoRoll = main.getPianoRoll();

        const auto runItem = [] (const juce::PopupMenu& menu, const juce::String& text)
        {
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            {
                if (it.getItem().text == text && it.getItem().action != nullptr)
                {
                    it.getItem().action();
                    return true;
                }
            }

            return false;
        };

        CHECK (runItem (pianoRoll.createScaleMenu(), "Minor"));
        CHECK (runItem (pianoRoll.createScaleMenu(), "A"));
        CHECK (model.getScale() == Scale { 9, ScaleType::minor });
        CHECK (runItem (pianoRoll.createScaleMenu(), "Snap notes to the scale"));
        CHECK (model.isSnappingToScale());

        model.setTool (Tool::select);
        CHECK (runItem (pianoRoll.createChordMenu(), "Scale chord"));
        CHECK (model.getChordStamp() == ChordType::scaleTriad);
        CHECK (model.getTool() == Tool::draw);
        CHECK (runItem (pianoRoll.createChordMenu(), "Off (single notes)"));
        CHECK_FALSE (model.getChordStamp().has_value());

        // With no notes there is nothing to detect from.
        const auto scaleMenu = pianoRoll.createScaleMenu();
        auto detectFound = false;

        for (juce::PopupMenu::MenuItemIterator it (scaleMenu, true); it.next();)
        {
            if (it.getItem().text == "Detect from the notes")
            {
                detectFound = true;
                CHECK_FALSE (it.getItem().isEnabled);
            }
        }

        CHECK (detectFound);

        // A minor melody, for the picture.
        model.beginGesture ("test");

        for (const auto& [beat, pitch] : { std::pair { 0.0, 57 }, { 1.0, 60 }, { 2.0, 64 }, { 3.0, 62 },
                                           { 4.0, 60 }, { 5.0, 59 }, { 6.0, 57 }, { 7.5, 64 } })
        {
            Note n;
            n.pitch = pitch;
            n.startBeat = beat;
            n.lengthBeats = 0.75;
            model.addNote (n);
        }

        pianoRoll.showClip();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-scale.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
