#include <doctest.h>

#include "core/Clips.h"
#include "core/Markers.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
using namespace harmonia::ui::pianoroll;
namespace clips = core::clips;

namespace
{
    core::Note makeNote (int pitch, double start, double length = 1.0)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> at, juce::Point<float> downAt, bool dragged)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downAt, now, 1, dragged);
    }

    /** A piano roll on the test clip (beats 0 to 16), laid out, at 80 pixels a beat from beat 0. */
    struct PianoRollOnClip : test::ProjectWithClip
    {
        PianoRollOnClip()
        {
            model.setClip (clip);
            model.setSnap (core::snap::Mode::quarterBeat);
            pianoRoll.setBounds (0, 0, 1280, 700);
            pianoRoll.showClip();
            pianoRoll.getView().pixelsPerBeat = 80.0;
            pianoRoll.getView().leftBeat = 0.0;
        }

        PianoRollModel model { *project };
        audio::Transport transport { project->getEdit() };
        PianoRoll pianoRoll { model, transport };
    };
}

TEST_SUITE ("piano roll ruler and zoom to fit")
{
    TEST_CASE ("Z fits the clip and all its notes, or just the selected notes (Russian layout too)")
    {
        PianoRollOnClip f;
        auto& view = f.pianoRoll.getView();

        f.model.beginGesture ("notes");
        f.model.addNote (makeNote (48, 0.0));
        auto* high = f.model.addNote (makeNote (72, 14.0, 2.0));
        f.model.addNote (makeNote (60, 4.0, 0.5));
        f.model.deselectAll();

        // Far in, somewhere in the middle.
        view.pixelsPerBeat = 400.0;
        view.leftBeat = 8.0;
        view.rowHeight = 36.0f;

        CHECK (f.pianoRoll.keyPressed (juce::KeyPress ('z')));
        CHECK (view.beatToX (0.0) >= 0.0f);
        CHECK (view.beatToX (16.0) <= view.width);
        CHECK (view.beatToX (16.0) - view.beatToX (0.0) > view.width * 0.9f);
        CHECK (view.pitchToY (72) >= 0.0f);
        CHECK (view.pitchToY (48) + view.rowHeight <= view.height);

        // Just the selected note: Я is on the Z key.
        REQUIRE (high != nullptr);
        f.model.setSelected (*high, true);
        CHECK (f.pianoRoll.keyPressed (juce::KeyPress ('Z', juce::ModifierKeys(), 0x044f)));
        CHECK (view.beatToX (14.0) >= 0.0f);
        CHECK (view.beatToX (16.0) <= view.width);
        CHECK (view.beatToX (16.0) - view.beatToX (14.0) > view.width * 0.9f);
        CHECK (view.rowHeight == doctest::Approx (View::maxRowHeight));
        CHECK (view.pitchToY (72) >= 0.0f);
        CHECK (view.pitchToY (72) + view.rowHeight <= view.height);

        // In the Tools menu too; without a clip it does nothing.
        bool found = false;

        const auto menu = f.pianoRoll.createToolsMenu();

        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == "Zoom to fit")
                found = it.getItem().isEnabled && it.getItem().shortcutKeyDescription == "Z";

        CHECK (found);

        f.model.setClip (nullptr);
        const auto before = view.pixelsPerBeat;
        CHECK (f.pianoRoll.keyPressed (juce::KeyPress ('z')));
        CHECK (view.pixelsPerBeat == before);
    }

    TEST_CASE ("the song's markers show on the ruler; a click on one plays from it")
    {
        PianoRollOnClip f;
        auto& ruler = f.pianoRoll.getRuler();
        auto& view = f.pianoRoll.getView();

        f.project->beginGesture ("Markers");
        core::markers::add (f.project->getEdit(), 4.0, "Verse");
        core::markers::add (f.project->getEdit(), 12.0, "Chorus");

        REQUIRE (f.model.getMarkers().size() == 2);
        CHECK (f.model.getMarkers()[1].name == "Chorus");

        const auto chorusX = view.beatToX (12.0);
        REQUIRE (ruler.findMarkerAt ({ chorusX + 6.0f, 6.0f }).has_value());
        CHECK (ruler.findMarkerAt ({ chorusX + 6.0f, 6.0f })->name == "Chorus");
        CHECK_FALSE (ruler.findMarkerAt ({ chorusX + 6.0f, TimeRuler::markerLaneHeight + 6.0f }).has_value());
        CHECK_FALSE (ruler.findMarkerAt ({ view.beatToX (8.0), 6.0f }).has_value());

        std::optional<double> chosen;
        ruler.onPositionChosen = [&chosen] (double beat) { chosen = beat; };

        const juce::Point<float> onVerse { view.beatToX (4.0) + 8.0f, 6.0f };
        ruler.mouseDown (mouseAt (ruler, onVerse, onVerse, false));
        ruler.mouseUp (mouseAt (ruler, onVerse, onVerse, false));
        REQUIRE (chosen.has_value());
        CHECK (*chosen == doctest::Approx (4.0));

        // Below the markers a click still picks the place under the mouse.
        const juce::Point<float> below { view.beatToX (6.0), TimeRuler::markerLaneHeight + 8.0f };
        ruler.mouseDown (mouseAt (ruler, below, below, false));
        ruler.mouseUp (mouseAt (ruler, below, below, false));
        CHECK (*chosen == doctest::Approx (6.0));

        // Removing one (undo) updates the piano roll's list.
        f.project->undo();
        CHECK (f.model.getMarkers().empty());
    }

    TEST_CASE ("dragging a looping clip's bracket on the ruler changes its loop length in one undo step")
    {
        PianoRollOnClip f;
        auto& ruler = f.pianoRoll.getRuler();
        auto& view = f.pianoRoll.getView();

        core::NoteEditor (*f.clip).add ({ makeNote (60, 0.0), makeNote (64, 2.0) });
        clips::setEnd (*f.clip, 4.0, 0.25);
        clips::setLooping (*f.clip, true);
        clips::setEnd (*f.clip, 16.0, 0.25);
        f.project->clearUndoHistory();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        // A clip that doesn't loop has no bracket.
        REQUIRE (f.model.isLooping());
        REQUIRE (ruler.getLoopEndX().has_value());
        CHECK (*ruler.getLoopEndX() == doctest::Approx (view.beatToX (4.0)));
        CHECK (f.model.getWholeClipBeats().getStart() == doctest::Approx (0.0));
        CHECK (f.model.getWholeClipBeats().getEnd() == doctest::Approx (16.0));

        const juce::Point<float> down { view.beatToX (4.0) - 2.0f, TimeRuler::markerLaneHeight + 8.0f };
        ruler.mouseDown (mouseAt (ruler, down, down, false));
        ruler.mouseDrag (mouseAt (ruler, { view.beatToX (6.1), down.y }, down, true));
        ruler.mouseDrag (mouseAt (ruler, { view.beatToX (8.05), down.y }, down, true));
        ruler.mouseUp (mouseAt (ruler, { view.beatToX (8.05), down.y }, down, true));

        CHECK (clips::getLoopRange (*f.clip).getLength() == doctest::Approx (8.0));
        CHECK (f.model.getClipStartBeat() == doctest::Approx (0.0));
        CHECK (f.model.getClipEndBeat() == doctest::Approx (8.0));
        CHECK (f.model.getWholeClipBeats().getStart() == doctest::Approx (0.0));
        CHECK (f.model.getWholeClipBeats().getEnd() == doctest::Approx (16.0));
        CHECK (f.transport.getPositionSeconds() == doctest::Approx (0.0));    // the playhead didn't move

        f.project->undo();
        CHECK (clips::getLoopRange (*f.clip).getLength() == doctest::Approx (4.0));

        // Not shorter than a grid step.
        f.project->redo();
        ruler.mouseDown (mouseAt (ruler, { view.beatToX (8.0), down.y }, { view.beatToX (8.0), down.y }, false));
        ruler.mouseDrag (mouseAt (ruler, { -50.0f, down.y }, { view.beatToX (8.0), down.y }, true));
        ruler.mouseUp (mouseAt (ruler, { -50.0f, down.y }, { view.beatToX (8.0), down.y }, true));
        CHECK (clips::getLoopRange (*f.clip).getLength() == doctest::Approx (0.25));
    }

    TEST_CASE ("the main window: markers and a looping clip in the piano roll")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& model = main.getArrangementModel();
        auto& track = *project->getTracks().getFirst();
        track.setName ("Bass");

        auto* bass = project->addMidiClip (track, 0.0, 8.0, "Bass line");
        core::NoteEditor (*bass).add ({ makeNote (33, 0.0, 1.5), makeNote (36, 2.0, 1.0), makeNote (40, 4.0, 1.5), makeNote (38, 6.0, 2.0) });
        model.setLooping (*bass, true);
        clips::setEnd (*bass, 32.0, 0.25);

        model.addMarker (0.0, "Intro");
        model.addMarker (16.0, "Verse");

        model.openClip (bass);
        auto& pianoRoll = main.getPianoRoll();
        pianoRoll.zoomToFit();
        pianoRoll.getView().pixelsPerBeat = 30.0;     // the repeats in view too
        pianoRoll.getView().leftBeat = 0.0;
        pianoRoll.repaint();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-pianoroll-ruler.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
