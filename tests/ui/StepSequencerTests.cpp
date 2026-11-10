#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "audio/DrumKit.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/stepseq/StepSequencer.h"

using namespace harmonia;
using ui::stepseq::StepSequencerModel;

namespace
{
    constexpr int kick = 36, snare = 38, hat = 42;

    void dispatch (int ms = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** A drum track ("Drums", the drum machine) with a clip of one bar starting at beat 8, open
        in a piano roll model the step sequencer reads. */
    struct StepFixture
    {
        StepFixture()
        {
            track = project->getTracks().getFirst();
            track->setName ("Drums");
            REQUIRE (audio::instruments::useDrumKit (*track).isEmpty());
            clip = project->addMidiClip (*track, 8.0, 4.0, "Beat");
            REQUIRE (clip != nullptr);
            notes.setClip (clip);
            project->clearUndoHistory();
        }

        /** The clip's notes as (pitch, song beat) pairs, in time order. */
        std::vector<std::pair<int, double>> hits()
        {
            std::vector<std::pair<int, double>> result;

            for (const auto& n : notes.getNotes())
                result.push_back ({ n.values.pitch, n.values.startBeat });

            std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.second < b.second || (a.second == b.second && a.first < b.first); });
            return result;
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* track = nullptr;
        te::MidiClip* clip = nullptr;
        ui::pianoroll::PianoRollModel notes { *project };
        StepSequencerModel steps { notes };
    };

    core::Note makeNote (int pitch, double songBeat, double length = 0.25, int velocity = 100)
    {
        core::Note note;
        note.pitch = pitch;
        note.startBeat = songBeat;
        note.lengthBeats = length;
        note.velocity = velocity;
        return note;
    }
}

TEST_SUITE ("ui: step sequencer")
{
    TEST_CASE ("the clip's steps: sixteenths at first, other lengths by choice, from the clip's start")
    {
        StepFixture f;
        CHECK (f.steps.getStepsPerBeat() == 4);
        CHECK (f.steps.getNumSteps() == 16);
        CHECK (f.steps.getStepsPerBar() == 16);
        CHECK (f.steps.getStepBeat (0) == doctest::Approx (8.0));
        CHECK (f.steps.getStepBeat (5) == doctest::Approx (9.25));
        CHECK (f.steps.getStepAt (9.3) == 5);
        CHECK (f.steps.getStepAt (7.9) == -1);

        f.steps.setStepsPerBeat (2);
        CHECK (f.steps.getNumSteps() == 8);
        f.steps.setStepsPerBeat (3);
        CHECK (f.steps.getNumSteps() == 12);
        CHECK (StepSequencerModel::getStepName (3) == "1/8 triplets");
        f.steps.setStepsPerBeat (5);                        // not offered: stays
        CHECK (f.steps.getStepsPerBeat() == 3);
        f.steps.setStepsPerBeat (8);
        CHECK (f.steps.getNumSteps() == 32);

        // Without a clip: nothing.
        f.notes.setClip (nullptr);
        CHECK (f.steps.getNumSteps() == 0);
        CHECK (f.steps.getRows().empty());
        CHECK_FALSE (f.steps.toggle (kick, 0));
    }

    TEST_CASE ("the rows: the kit's main drums, the keys with notes, the keys with samples; named as the piano roll's keys")
    {
        StepFixture f;
        auto rows = f.steps.getRows();
        REQUIRE (rows.size() == 11);
        CHECK (rows.front().pitch == 51);
        CHECK (rows.front().name == "Ride");
        CHECK (rows.back().pitch == kick);
        CHECK (rows.back().name == "Kick");

        // A note on another key, and a sample on yet another: rows of their own.
        f.notes.beginGesture ("Add");
        f.notes.addNote (makeNote (60, 8.0));
        auto* drums = audio::instruments::get (*f.track);
        const auto file = test::writeTone (f.temp.getFile ("Boom.wav"), 0.2, 0.5f);
        REQUIRE (audio::drum_kit::setSample (*drums, 100, file).isEmpty());
        REQUIRE (audio::drum_kit::setSample (*drums, kick, file).isEmpty());

        rows = f.steps.getRows();
        REQUIRE (rows.size() == 13);
        CHECK (rows[0].pitch == 100);
        CHECK (rows[0].name == "Boom");
        CHECK (rows[1].pitch == 60);
        CHECK (rows.back().name == "Boom");                 // the kick's sample

        // Drum names off: the notes' names.
        f.notes.setDrumNamesShown (false);
        CHECK (f.steps.getRows().back().name == juce::MidiMessage::getMidiNoteName (kick, true, true, 4));
    }

    TEST_CASE ("a click puts a hit a step long, another takes it away; each is one undo step")
    {
        StepFixture f;
        CHECK_FALSE (f.steps.hasHit (kick, 0));

        CHECK (f.steps.toggle (kick, 0));
        CHECK (f.steps.toggle (kick, 4));
        CHECK (f.steps.toggle (snare, 4));
        CHECK (f.steps.hasHit (kick, 4));
        CHECK (f.steps.getVelocity (kick, 4) == StepSequencerModel::defaultVelocity);
        CHECK (f.hits() == std::vector<std::pair<int, double>> { { kick, 8.0 }, { kick, 9.0 }, { snare, 9.0 } });
        CHECK (f.project->getUndoDescription() == "Add hit");

        // The notes are ordinary notes of the clip, a step long.
        const auto& drawn = f.notes.getNotes();
        REQUIRE (drawn.size() == 3);
        CHECK (drawn[0].values.lengthBeats == doctest::Approx (0.25));

        CHECK_FALSE (f.steps.toggle (kick, 4));
        CHECK_FALSE (f.steps.hasHit (kick, 4));
        CHECK (f.project->getUndoDescription() == "Remove hit");

        f.project->undo();
        CHECK (f.steps.hasHit (kick, 4));
        f.project->undo();
        CHECK_FALSE (f.steps.hasHit (snare, 4));

        // Outside the clip: nothing.
        CHECK_FALSE (f.steps.toggle (kick, 16));
        CHECK_FALSE (f.steps.toggle (kick, -1));
        CHECK (f.hits().size() == 2);
    }

    TEST_CASE ("notes made in the piano roll show in the step they start in; a muted one is no hit")
    {
        StepFixture f;
        f.notes.beginGesture ("Add");
        f.notes.addNote (makeNote (kick, 9.1, 1.0, 60));     // off the grid, in the fifth step
        f.notes.addNote (makeNote (kick, 9.2, 0.1, 90));     // the same step, harder

        auto muted = makeNote (hat, 10.0);
        muted.muted = true;
        f.notes.addNote (muted);

        CHECK (f.steps.getVelocity (kick, 4) == 90);
        CHECK_FALSE (f.steps.hasHit (kick, 3));
        CHECK_FALSE (f.steps.hasHit (kick, 5));
        CHECK_FALSE (f.steps.hasHit (hat, 8));

        // A click on the step takes both away.
        CHECK_FALSE (f.steps.toggle (kick, 4));
        CHECK (f.hits() == std::vector<std::pair<int, double>> { { hat, 10.0 } });

        // A hit put where the muted note is takes its place.
        CHECK (f.steps.toggle (hat, 8));
        CHECK (f.hits().size() == 1);
        CHECK (f.steps.hasHit (hat, 8));
    }

    TEST_CASE ("harder and softer; a row and the whole clip cleared")
    {
        StepFixture f;
        f.steps.toggle (kick, 0);
        f.steps.toggle (kick, 8);
        f.steps.toggle (hat, 2);

        f.steps.nudgeVelocity (kick, 0, 10);
        CHECK (f.steps.getVelocity (kick, 0) == 110);
        CHECK (f.project->getUndoDescription() == "Hit velocity");
        f.steps.nudgeVelocity (kick, 0, 100);
        CHECK (f.steps.getVelocity (kick, 0) == 127);
        f.steps.nudgeVelocity (kick, 0, -500);
        CHECK (f.steps.getVelocity (kick, 0) == 1);
        f.steps.nudgeVelocity (kick, 5, 10);                 // an empty cell: nothing
        CHECK_FALSE (f.steps.hasHit (kick, 5));
        CHECK (f.steps.getVelocity (kick, 8) == 100);

        f.steps.clearRow (kick);
        CHECK (f.hits() == std::vector<std::pair<int, double>> { { hat, 8.5 } });
        CHECK (f.project->getUndoDescription() == "Clear row");
        f.project->undo();
        CHECK (f.hits().size() == 3);

        f.steps.clearAll();
        CHECK (f.hits().empty());
        f.project->undo();
        CHECK (f.hits().size() == 3);
    }

    TEST_CASE ("the grid: a click, a drag along the row as one undo step, the right button, Alt+wheel")
    {
        StepFixture f;
        audio::Transport transport (f.project->getEdit());
        ui::stepseq::StepSequencer sequencer (f.notes, transport);
        sequencer.setBounds (0, 0, 900, 360);
        dispatch();

        const auto rows = sequencer.getModel().getRows();
        const auto kickRow = (int) rows.size() - 1;
        REQUIRE (rows[(size_t) kickRow].pitch == kick);

        // Where the cells are.
        const auto cell = sequencer.getCellBounds (kickRow, 4);
        REQUIRE (sequencer.getCellAt (cell.getCentre()).has_value());
        CHECK (*sequencer.getCellAt (cell.getCentre()) == std::pair<int, int> { kickRow, 4 });
        CHECK_FALSE (sequencer.getCellAt ({ cell.getCentreX(), 4 }).has_value());         // the bar numbers
        CHECK (sequencer.getGridSize().x >= 16 * cell.getWidth());

        // A click, then dragged fast to four steps on: every step on the way.
        sequencer.gridDown (cell.getCentre(), false);
        sequencer.gridDrag (sequencer.getCellBounds (kickRow, 8).getCentre());
        sequencer.gridUp();

        for (int step = 4; step <= 8; ++step)
            CHECK (sequencer.getModel().hasHit (kick, step));

        CHECK_FALSE (sequencer.getModel().hasHit (kick, 3));
        CHECK_FALSE (sequencer.getModel().hasHit (kick, 9));

        f.project->undo();
        CHECK (f.hits().empty());
        f.project->redo();
        CHECK (f.hits().size() == 5);

        // A click on a hit takes it away; dragging on takes those away too.
        sequencer.gridDown (sequencer.getCellBounds (kickRow, 8).getCentre(), false);
        sequencer.gridDrag (sequencer.getCellBounds (kickRow, 7).getCentre());
        sequencer.gridUp();
        CHECK (f.hits().size() == 3);

        // The right button only takes away.
        sequencer.gridDown (sequencer.getCellBounds (kickRow, 0).getCentre(), true);
        CHECK (f.hits().size() == 3);
        sequencer.gridDrag (sequencer.getCellBounds (kickRow, 5).getCentre());
        sequencer.gridUp();
        CHECK (f.hits() == std::vector<std::pair<int, double>> { { kick, 9.5 } });
        f.project->undo();
        CHECK (f.hits().size() == 3);

        // Alt+wheel: harder.
        sequencer.gridWheel (sequencer.getCellBounds (kickRow, 6).getCentre(), 0.5f);
        CHECK (sequencer.getModel().getVelocity (kick, 6) == 110);

        // The row's menu clears it; the Clear button, everything.
        sequencer.getModel().toggle (snare, 4);
        const auto rowMenu = sequencer.createRowMenu (kick);
        juce::PopupMenu::MenuItemIterator it (rowMenu);
        REQUIRE (it.next());
        it.getItem().action();
        CHECK (f.hits() == std::vector<std::pair<int, double>> { { snare, 9.0 } });
        sequencer.getClearButton().onClick();
        CHECK (f.hits().empty());

        // Another step length from the box.
        sequencer.getStepBox().setSelectedId (2, juce::sendNotificationSync);
        CHECK (sequencer.getModel().getNumSteps() == 8);
        CHECK_FALSE (sequencer.getPlayingStep().has_value());
        CHECK (sequencer.createComponentSnapshot (sequencer.getLocalBounds()).isValid());
    }

    TEST_CASE ("main window: Alt+T, the Steps and Piano roll buttons and the View menu swap the piano roll and the steps")
    {
        StepFixture f;
        auto* audioTrack = f.project->addTrack ("Audio");
        core::tracks::setKind (*audioTrack, core::tracks::Kind::audio);
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        main->getArrangementModel().openClip (f.clip);
        dispatch();

        auto& pianoRoll = main->getPianoRoll();
        auto& sequencer = main->getStepSequencer();
        CHECK (pianoRoll.isVisible());
        CHECK_FALSE (sequencer.isVisible());
        CHECK_FALSE (main->areStepsShown());

        const juce::KeyPress altT ('t', juce::ModifierKeys::altModifier, 0);
        CHECK (main->keyPressed (altT));
        CHECK (main->areStepsShown());
        CHECK (sequencer.isVisible());
        CHECK_FALSE (pianoRoll.isVisible());
        CHECK (sequencer.getBounds() == pianoRoll.getBounds());
        CHECK (sequencer.getModel().getNumSteps() == 16);

        // A hit made there is a note of the clip the piano roll shows.
        CHECK (sequencer.getModel().toggle (kick, 0));
        CHECK (main->getPianoRollModel().getNotes().size() == 1);

        for (const auto step : { 4, 8, 12 })
            sequencer.getModel().toggle (kick, step);

        for (const auto step : { 4, 12 })
            sequencer.getModel().toggle (snare, step);

        for (int step = 0; step < 16; step += 2)
            sequencer.getModel().toggle (hat, step);

        sequencer.getModel().nudgeVelocity (hat, 4, -50);
        dispatch();
        CHECK (main->getPianoRollModel().getNotes().size() == 14);
        CHECK (main->createComponentSnapshot (main->getLocalBounds()).isValid());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-steps.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (main->createComponentSnapshot (main->getLocalBounds()), out);
        }

        // The View menu has it ticked; its button goes back.
        const auto findItem = [] (const juce::PopupMenu& menu, const juce::String& text) -> std::optional<juce::PopupMenu::Item>
        {
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (it.getItem().text.startsWith (text))
                    return it.getItem();

            return std::nullopt;
        };

        REQUIRE (findItem (main->getMainMenu().createViewMenu(), "Step sequencer").has_value());
        CHECK (findItem (main->getMainMenu().createViewMenu(), "Step sequencer")->isTicked);
        sequencer.getNotesButton().onClick();
        CHECK (pianoRoll.isVisible());
        CHECK_FALSE (sequencer.isVisible());
        CHECK_FALSE (findItem (main->getMainMenu().createViewMenu(), "Step sequencer")->isTicked);

        pianoRoll.getStepsButton().onClick();
        CHECK (sequencer.isVisible());

        // Behind the mixer it isn't shown; Alt+T brings the editor (and the steps) forward.
        main->showLowerPanel (ui::MainComponent::LowerPanel::mixer);
        CHECK_FALSE (sequencer.isVisible());
        main->showLowerPanel (ui::MainComponent::LowerPanel::pianoRoll);
        CHECK (sequencer.isVisible());

        // An audio track has its audio clip editor there, and no steps.
        main->getArrangementModel().setCurrentTrack (audioTrack);
        dispatch();
        CHECK_FALSE (sequencer.isVisible());
        CHECK (main->getAudioEditor().isVisible());
        CHECK_FALSE (findItem (main->getMainMenu().createViewMenu(), "Step sequencer")->isEnabled);

        main.reset();
    }
}
