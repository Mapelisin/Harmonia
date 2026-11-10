#include <doctest.h>

#include "core/TestProject.h"
#include "audio/Instruments.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "ui/MainComponent.h"
#include "ui/arrangement/ArrangementGestures.h"

using namespace harmonia;
using namespace harmonia::ui::arrangement;
namespace clips = core::clips;
namespace links = core::clip_links;

namespace
{
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    constexpr int right = juce::ModifierKeys::rightButtonModifier;
    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int ctrl = juce::ModifierKeys::ctrlModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;

    /** One 4-bar clip at the start of the first track; the arrangement at 16 pixels per beat,
        snapping to bars. */
    struct ArrangementFixture : test::ProjectWithClip
    {
        ArrangementFixture()
        {
            view.width = 1000.0f;
            view.height = 400.0f;
            view.pixelsPerBeat = 16.0;
            model.setPixelsPerBeat (view.pixelsPerBeat);
            model.setSnap (core::snap::Mode::bar);
            project->clearUndoHistory();
        }

        te::AudioTrack* addTrack (const juce::String& name = "Two")
        {
            auto* added = project->addTrack (name);
            view.numTracks = model.getNumTracks();
            return added;
        }

        /** A point in the row of `track`, below the clip's header. */
        juce::Point<float> at (double beat, int trackIndex, float yInRow = 30.0f) const
        {
            return { view.beatToX (beat), view.trackToY (trackIndex) + yInRow };
        }

        /** A point on a clip's name strip (its header): where a clip is taken to be moved. */
        juce::Point<float> onName (double beat, int trackIndex) const       { return at (beat, trackIndex, 7.0f); }

        void click (juce::Point<float> position, int mods = 0, int clicks = 1)
        {
            gestures.down ({ position, juce::ModifierKeys (mods | left), clicks });
            gestures.up ({ position, juce::ModifierKeys (mods | left), clicks });
        }

        void drag (juce::Point<float> from, juce::Point<float> to, int mods = 0, int button = left)
        {
            const juce::ModifierKeys keys (mods | button);
            gestures.down ({ from, keys });

            for (int i = 1; i <= 10; ++i)
                gestures.drag ({ from + (to - from) * ((float) i / 10.0f), keys });

            gestures.up ({ to, keys });
        }

        juce::Array<te::MidiClip*> clipsOn (te::AudioTrack& t) const    { return clips::getMidiClips (t); }

        ArrangementModel model { *project };
        ArrangementView view;
        ArrangementGestures gestures { model, view };
    };

    core::Note makeNote (int pitch, double start)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = 1.0;
        return n;
    }
}

TEST_SUITE ("arrangement: model")
{
    TEST_CASE ("at the start the first track is current and its first clip is open")
    {
        ArrangementFixture f;
        CHECK (f.model.getCurrentTrack() == f.track);
        CHECK (f.model.getOpenClip() == f.clip);
        REQUIRE (f.model.getClips().size() == 1);
        CHECK (f.model.getClips()[0].open);
    }

    TEST_CASE ("a new track gets the built-in synth and a clip where the open clip is; undo takes it away")
    {
        ArrangementFixture f;
        int changes = 0;
        f.model.onCurrentChanged = [&changes] { ++changes; };

        auto* track = f.model.addTrack();
        REQUIRE (track != nullptr);
        CHECK (f.model.getNumTracks() == 2);
        CHECK (audio::instruments::get (*track) != nullptr);
        CHECK (f.model.getCurrentTrack() == track);
        REQUIRE (f.clipsOn (*track).size() == 1);
        CHECK (f.model.getOpenClip() == f.clipsOn (*track).getFirst());
        CHECK (clips::getStartBeat (*f.model.getOpenClip()) == doctest::Approx (0.0));
        CHECK (clips::getEndBeat (*f.model.getOpenClip()) == doctest::Approx (16.0));
        CHECK (changes == 1);

        f.model.undo();
        CHECK (f.model.getNumTracks() == 1);
        CHECK (f.model.getCurrentTrack() == f.track);
        CHECK (f.model.getOpenClip() == f.clip);
    }

    TEST_CASE ("deleting a track deletes its clips; a linked copy left alone stops being linked")
    {
        ArrangementFixture f;
        auto* second = f.addTrack();
        auto* copy = links::duplicate (*f.clip, 16.0, true);
        REQUIRE (copy != nullptr);
        clips::move (*copy, 16.0, *second);
        f.model.openClip (copy);
        CHECK (f.model.getCurrentTrack() == second);

        f.model.deleteTrack (*second);
        CHECK (f.model.getNumTracks() == 1);
        CHECK_FALSE (links::isLinked (*f.clip));
        CHECK (f.model.getCurrentTrack() == f.track);
        CHECK (f.model.getOpenClip() == f.clip);

        f.model.undo();
        CHECK (f.model.getNumTracks() == 2);
        CHECK (links::isLinked (*f.clip));
    }

    TEST_CASE ("Ctrl+B makes linked copies after the selection, Ctrl+Shift+B independent ones")
    {
        ArrangementFixture f;
        core::NoteEditor (*f.clip).add (makeNote (60, 1.0));
        f.model.setSelection ({ f.clip });

        f.model.duplicateSelected (true);
        auto onTrack = f.clipsOn (*f.track);
        REQUIRE (onTrack.size() == 2);
        auto* linked = onTrack[1];
        CHECK (clips::getStartBeat (*linked) == doctest::Approx (16.0));
        CHECK (links::getGroup (*f.clip).contains (linked));
        CHECK (f.model.getSelectedClips() == juce::Array<te::Clip*> { linked });

        f.model.duplicateSelected (false);
        onTrack = f.clipsOn (*f.track);
        REQUIRE (onTrack.size() == 3);
        CHECK (clips::getStartBeat (*onTrack[2]) == doctest::Approx (32.0));
        CHECK_FALSE (links::isLinked (*onTrack[2]));
        CHECK (test::readNotes (*onTrack[2]) == test::readNotes (*f.clip));

        // A short clip is copied to the next whole bar.
        f.model.setSelection ({ onTrack[2] });
        clips::setEnd (*onTrack[2], 33.0, 0.25);
        f.model.duplicateSelected (false);
        CHECK (clips::getStartBeat (*f.clipsOn (*f.track).getLast()) == doctest::Approx (36.0));

        f.model.undo();
        f.model.undo();
        f.model.undo();
        CHECK (f.clipsOn (*f.track).size() == 1);
    }

    TEST_CASE ("deleting the open clip leaves nothing open; undo brings it back but not into the selection")
    {
        ArrangementFixture f;
        f.model.setSelection ({ f.clip });
        f.model.deleteSelected();

        CHECK (f.clipsOn (*f.track).isEmpty());
        CHECK (f.model.getOpenClip() == nullptr);
        CHECK (f.model.getCurrentTrack() == f.track);
        CHECK (f.model.getSelectedClips().isEmpty());

        f.model.undo();
        CHECK (f.clipsOn (*f.track).size() == 1);

        // A clip created and then undone is gone from the selection and the piano roll.
        auto* created = f.model.createClip (*f.track, 21.0);
        REQUIRE (created != nullptr);
        CHECK (f.model.getOpenClip() == created);
        f.model.undo();
        CHECK (f.model.getOpenClip() == nullptr);
        CHECK (f.model.getSelectedClips().isEmpty());
    }

    TEST_CASE ("Play loops the open clip in clip mode, the loop region or the whole song in song mode")
    {
        ArrangementFixture f;
        auto range = f.model.getPlayRange();
        CHECK (range.start == doctest::Approx (0.0));
        CHECK (range.end == doctest::Approx (16.0));

        f.project->addMidiClip (*f.track, 16.0, 2.0);
        f.model.setPlayMode (PlayMode::song);
        range = f.model.getPlayRange();
        CHECK (range.start == doctest::Approx (0.0));
        CHECK (range.end == doctest::Approx (20.0));      // to the end of the last bar

        f.model.setLoopRegion ({ 8.0, 4.0 });
        range = f.model.getPlayRange();
        CHECK (range.start == doctest::Approx (4.0));
        CHECK (range.end == doctest::Approx (8.0));

        f.model.clearLoopRegion();
        CHECK (f.model.getPlayRange().end == doctest::Approx (20.0));

        f.model.setPlayMode (PlayMode::clip);
        f.model.openClip (nullptr);
        CHECK (f.model.getPlayRange().getLength() == doctest::Approx (0.0));
    }

    TEST_CASE ("renaming tracks and clips is undoable; empty names are ignored")
    {
        ArrangementFixture f;
        auto* second = f.addTrack ("Keys");
        auto* named = f.project->addMidiClip (*second, 0.0, 4.0, "Keys");
        auto* own = f.project->addMidiClip (*second, 4.0, 4.0, "Hook");

        f.model.renameTrack (*second, "  Piano ");
        CHECK (second->getName() == "Piano");
        CHECK (named->getName() == "Piano");    // named after the track: follows it
        CHECK (own->getName() == "Hook");

        f.model.renameTrack (*f.track, "Drums");
        f.model.renameClip (*f.clip, "Verse");
        f.model.renameClip (*f.clip, "   ");

        CHECK (f.track->getName() == "Drums");
        CHECK (f.clip->getName() == "Verse");

        f.model.undo();
        CHECK (f.clip->getName() != "Verse");
    }
}

TEST_SUITE ("arrangement: mouse")
{
    TEST_CASE ("clicking a clip selects and opens it, Ctrl+click adds to the selection")
    {
        ArrangementFixture f;
        auto* second = f.addTrack();
        auto* other = f.project->addMidiClip (*second, 16.0, 4.0);

        f.click (f.at (17.0, 1));
        CHECK (f.model.getOpenClip() == other);
        CHECK (f.model.getCurrentTrack() == second);
        CHECK (f.model.getSelectedClips() == juce::Array<te::Clip*> { other });

        f.click (f.at (4.0, 0), ctrl);
        CHECK (f.model.getOpenClip() == f.clip);
        CHECK (f.model.getSelectedClips().size() == 2);

        f.click (f.at (17.0, 1), ctrl);
        CHECK (f.model.getSelectedClips() == juce::Array<te::Clip*> { f.clip });

        // A click on empty space clears the selection.
        f.click (f.at (40.0, 0));
        CHECK (f.model.getSelectedClips().isEmpty());
    }

    TEST_CASE ("a dragged clip moves along the bar grid and to another track, as one undo step")
    {
        ArrangementFixture f;
        auto* second = f.addTrack();
        core::NoteEditor (*f.clip).add (makeNote (60, 1.0));

        // 5.2 beats to the right lands on bar 2 (beat 4).
        f.drag (f.onName (4.0, 0), f.at (9.2, 1));
        CHECK (f.clipsOn (*f.track).isEmpty());
        REQUIRE (f.clipsOn (*second).size() == 1);
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (4.0));
        CHECK (test::readNotes (*f.clip).size() == 1);
        CHECK (f.model.getOpenClip() == f.clip);

        f.model.undo();
        REQUIRE (f.clipsOn (*f.track).size() == 1);
        CHECK (clips::getStartBeat (*f.clipsOn (*f.track).getFirst()) == doctest::Approx (0.0));

        // Alt: no snap.
        f.drag (f.onName (4.0, 0), f.at (6.5, 0), alt);
        CHECK (clips::getStartBeat (*f.clipsOn (*f.track).getFirst()) == doctest::Approx (2.5));

        // Never before the song's start or off the tracks.
        f.drag (f.onName (4.0, 0), f.at (-30.0, 5));
        CHECK (clips::getStartBeat (*f.clipsOn (*second).getFirst()) == doctest::Approx (0.0));
    }

    TEST_CASE ("the whole selection moves together")
    {
        ArrangementFixture f;
        auto* other = f.project->addMidiClip (*f.track, 16.0, 4.0);
        f.model.setSelection ({ f.clip, other });

        f.drag (f.onName (2.0, 0), f.at (10.0, 0));
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (8.0));
        CHECK (clips::getStartBeat (*other) == doctest::Approx (24.0));
        CHECK (f.model.getSelectedClips().size() == 2);
    }

    TEST_CASE ("Shift+drag makes a linked copy, Ctrl+drag an independent one; the original stays")
    {
        ArrangementFixture f;
        core::NoteEditor (*f.clip).add (makeNote (60, 1.0));

        f.drag (f.onName (4.0, 0), f.at (20.0, 0), shift);
        auto onTrack = f.clipsOn (*f.track);
        REQUIRE (onTrack.size() == 2);
        CHECK (clips::getStartBeat (*onTrack[0]) == doctest::Approx (0.0));
        CHECK (clips::getStartBeat (*onTrack[1]) == doctest::Approx (16.0));
        CHECK (links::getGroup (*f.clip).size() == 2);
        CHECK (f.model.getSelectedClips() == juce::Array<te::Clip*> { onTrack[1] });
        CHECK (f.model.getOpenClip() == onTrack[1]);

        f.model.undo();
        CHECK (f.clipsOn (*f.track).size() == 1);
        CHECK_FALSE (links::isLinked (*f.clip));

        f.drag (f.onName (4.0, 0), f.at (36.0, 0), ctrl);
        onTrack = f.clipsOn (*f.track);
        REQUIRE (onTrack.size() == 2);
        CHECK (clips::getStartBeat (*onTrack[1]) == doctest::Approx (32.0));
        CHECK_FALSE (links::isLinked (*onTrack[1]));
        CHECK (test::readNotes (*onTrack[1]) == test::readNotes (*f.clip));
    }

    TEST_CASE ("dragging a clip's edges trims it on the grid")
    {
        ArrangementFixture f;
        const auto y = f.view.trackToY (0) + 30.0f;

        f.drag ({ f.view.beatToX (16.0) - 2.0f, y }, { f.view.beatToX (7.0), y });
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (8.0));

        f.drag ({ f.view.beatToX (0.0) + 2.0f, y }, { f.view.beatToX (3.0), y });
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (4.0));
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (8.0));

        f.model.undo();
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (0.0));
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (8.0));

        // At least a bar long with bar snap.
        f.drag ({ f.view.beatToX (8.0) - 2.0f, y }, { f.view.beatToX (-2.0), y });
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (4.0));
    }

    TEST_CASE ("the right button deletes clips under the mouse, also along a drag, in one undo step")
    {
        ArrangementFixture f;
        f.project->addMidiClip (*f.track, 16.0, 4.0);
        f.project->addMidiClip (*f.track, 24.0, 4.0);
        f.project->clearUndoHistory();

        f.drag (f.at (2.0, 0), f.at (18.0, 0), 0, right);
        REQUIRE (f.clipsOn (*f.track).size() == 1);
        CHECK (clips::getStartBeat (*f.clipsOn (*f.track).getFirst()) == doctest::Approx (24.0));
        CHECK (f.model.getOpenClip() == nullptr);

        f.model.undo();
        CHECK (f.clipsOn (*f.track).size() == 3);
    }

    TEST_CASE ("Shift+drag on empty space selects the clips in the rectangle (a plain drag selects time)")
    {
        ArrangementFixture f;
        auto* second = f.addTrack();
        auto* other = f.project->addMidiClip (*second, 16.0, 4.0);
        auto* far = f.project->addMidiClip (*second, 40.0, 4.0);

        // Without Shift: a piece of time, no clip selected (see TimeSelectionTests).
        f.drag (f.at (30.0, 0), f.at (10.0, 1));
        CHECK (f.model.getSelectedClips().isEmpty());
        CHECK (f.model.getTimeSelection().has_value());
        f.model.deselectAll();

        f.drag (f.at (30.0, 0), f.at (10.0, 1), shift);
        auto selected = f.model.getSelectedClips();
        CHECK (selected.size() == 2);
        CHECK (selected.contains (f.clip));
        CHECK (selected.contains (other));

        // Shift adds to it.
        f.drag (f.at (38.0, 0), f.at (42.0, 1), shift);
        CHECK (f.model.getSelectedClips().size() == 3);
        CHECK (f.model.getSelectedClips().contains (far));
    }

    TEST_CASE ("a double-click on empty space makes a one-bar clip in that bar and opens it")
    {
        ArrangementFixture f;
        f.click (f.at (21.0, 0), 0, 1);
        f.click (f.at (21.0, 0), 0, 2);

        auto onTrack = f.clipsOn (*f.track);
        REQUIRE (onTrack.size() == 2);
        CHECK (clips::getStartBeat (*onTrack[1]) == doctest::Approx (20.0));
        CHECK (clips::getEndBeat (*onTrack[1]) == doctest::Approx (24.0));
        CHECK (f.model.getOpenClip() == onTrack[1]);

        f.model.undo();
        CHECK (f.clipsOn (*f.track).size() == 1);
    }

    TEST_CASE ("the mark at a clip's top left asks for its menu; edges and marks set the cursor")
    {
        ArrangementFixture f;
        te::Clip* asked = nullptr;
        f.gestures.onClipMenuRequested = [&asked] (te::Clip& clip, juce::Point<float>) { asked = &clip; };

        f.click ({ f.view.beatToX (0.0) + 5.0f, f.view.trackToY (0) + 7.0f });
        CHECK (asked == f.clip);
        CHECK (f.clipsOn (*f.track).size() == 1);

        using Pointer = ArrangementGestures::Pointer;
        CHECK (f.gestures.getCursorFor (Pointer { { f.view.beatToX (16.0) - 2.0f, 30.0f } }) == juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor));
        // A clip's name strip takes the clip; below it (and on empty rows) a drag selects time.
        CHECK (f.gestures.getCursorFor (Pointer { { f.view.beatToX (8.0), 7.0f } }) == juce::MouseCursor (juce::MouseCursor::NormalCursor));
        CHECK (f.gestures.getCursorFor (Pointer { { f.view.beatToX (8.0), 30.0f } }) == juce::MouseCursor (juce::MouseCursor::IBeamCursor));
        CHECK (f.gestures.getCursorFor (Pointer { { f.view.beatToX (40.0), 30.0f } }) == juce::MouseCursor (juce::MouseCursor::IBeamCursor));

        // Over the selected piece of time: it can be taken.
        f.model.setTimeSelection (4.0, 12.0, 0, 0);
        CHECK (f.gestures.getCursorFor (Pointer { { f.view.beatToX (8.0), 30.0f } }) == juce::MouseCursor (juce::MouseCursor::DraggingHandCursor));
    }
}

TEST_SUITE ("arrangement: main window")
{
    struct MainFixture
    {
        MainFixture()
        {
            auto& track = *project->getTracks().getFirst();
            audio::instruments::useBuiltInSynth (track);
            clip = project->addMidiClip (track, 0.0, 16.0, "Main");
            project->clearUndoHistory();
            main = std::make_unique<ui::MainComponent> (test::session(), *project);
            main->setBounds (0, 0, 1280, 800);
        }

        ~MainFixture()
        {
            main.reset();
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::MidiClip* clip = nullptr;
        std::unique_ptr<ui::MainComponent> main;
    };

    TEST_CASE ("the piano roll edits the clip opened in the arrangement")
    {
        MainFixture f;
        auto& arrangement = f.main->getArrangementModel();
        auto& pianoRoll = f.main->getPianoRollModel();
        CHECK (pianoRoll.getClip() == f.clip);

        auto* track = arrangement.addTrack();
        REQUIRE (track != nullptr);
        CHECK (pianoRoll.getClip() == arrangement.getOpenClip());
        CHECK (pianoRoll.getClip() != f.clip);

        arrangement.openClip (f.clip);
        CHECK (pianoRoll.getClip() == f.clip);

        // The open clip deleted: the piano roll shows nothing and still paints.
        arrangement.setSelection ({ f.clip });
        CHECK (f.main->getArrangement().keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
        CHECK (pianoRoll.getClip() == nullptr);
        CHECK (f.main->createComponentSnapshot (f.main->getLocalBounds()).isValid());

        // Undo (Ctrl+Z) works from the main window.
        CHECK (f.main->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0)));
        CHECK (core::clips::getMidiClips (*f.project->getTracks().getFirst()).size() == 1);
    }

    TEST_CASE ("the open clip dragged to another track stays in the piano roll")
    {
        MainFixture f;
        auto& arrangement = f.main->getArrangementModel();
        auto* track = arrangement.addTrack();
        arrangement.openClip (f.clip);

        f.project->beginGesture ("Move");
        clips::move (*f.clip, 4.0, *track);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        CHECK (f.main->getPianoRollModel().getClip() == f.clip);
        CHECK (arrangement.getOpenClip() == f.clip);
        CHECK (arrangement.getCurrentTrack() == track);
        CHECK (f.main->getPianoRollModel().getClipStartBeat() == doctest::Approx (4.0));
    }

    TEST_CASE ("the transport loops what the mode says")
    {
        MainFixture f;
        auto& transport = f.project->getEdit().getTransport();
        CHECK (transport.looping.get());
        CHECK (transport.getLoopRange().getEnd().inSeconds() == doctest::Approx (8.0));   // 16 beats at 120 BPM

        f.project->addMidiClip (*f.project->getTracks().getFirst(), 16.0, 16.0, "Second");
        f.main->keyPressed (juce::KeyPress ('l', 0, 0));
        CHECK (f.main->getArrangementModel().getPlayMode() == PlayMode::song);
        CHECK (transport.getLoopRange().getEnd().inSeconds() == doctest::Approx (16.0));
    }

    TEST_CASE ("the main window paints an arrangement of several tracks")
    {
        MainFixture f;
        auto& arrangement = f.main->getArrangementModel();
        f.main->getPianoRollModel().beginGesture ("notes");

        for (int i = 0; i < 16; ++i)
            f.main->getPianoRollModel().addNote (makeNote (48 + (i * 5) % 24, i * 0.5));

        auto* copy = links::duplicate (*f.clip, 16.0, true);
        REQUIRE (copy != nullptr);
        links::duplicate (*f.clip, 32.0, false);

        for (const auto* name : { "Bass", "Lead", "Pad" })
        {
            auto* track = arrangement.addTrack();
            arrangement.renameTrack (*track, name);
            auto* clip = arrangement.getOpenClip();
            core::NoteEditor editor (*clip);

            for (int i = 0; i < 8; ++i)
                editor.add (makeNote (36 + (i * 7) % 30, i * 2.0));

            clips::move (*clip, 8.0, *track);
        }

        arrangement.getTracks()[2]->setMute (true);
        arrangement.setSelection ({ copy });
        arrangement.openClip (f.clip);
        arrangement.setPlayMode (PlayMode::song);
        arrangement.setLoopRegion ({ 8.0, 24.0 });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto image = f.main->createComponentSnapshot (f.main->getLocalBounds());
        CHECK (image.isValid());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-arrangement.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
