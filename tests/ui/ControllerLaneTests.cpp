#include <doctest.h>

#include "core/Clips.h"
#include "core/Controllers.h"
#include "core/TestProject.h"
#include "ui/NoteInput.h"
#include "ui/pianoroll/ControlLane.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace controllers = core::controllers;
using controllers::Controller;

namespace
{
    struct FakeClock final : audio::SongClock
    {
        bool isPlaying() const override                             { return playing; }
        std::optional<double> getHeardBeat (double) const override  { return playing ? std::optional<double> (beat) : std::nullopt; }
        juce::Range<double> getLoopBeats() const override           { return loop; }

        bool playing = false;
        double beat = 0.0;
        juce::Range<double> loop;
    };

    /** A 4-bar clip from beat 4 (bar 2) open in the piano roll. */
    struct LaneFixture
    {
        LaneFixture()
        {
            clip = project->addMidiClip (*project->getTracks().getFirst(), 4.0, 16.0, "Clip");
            model.setClip (clip);
            project->clearUndoHistory();
        }

        ~LaneFixture()
        {
            input.reset();
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::MidiClip* clip = nullptr;
        pianoroll::PianoRollModel model { *project };
        audio::NotePreview preview;
        FakeClock clock;
        std::unique_ptr<NoteInput> input = std::make_unique<NoteInput> (model, preview, clock);
    };

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                 p, juce::Time::getCurrentTime(), 1, false);
    }
}

TEST_SUITE ("piano roll: controller lanes")
{
    TEST_CASE ("F goes through the note values, then pitch bend, mod wheel and the pedal")
    {
        LaneFixture f;
        const auto numProperties = (int) core::note_property::getAll().size();
        CHECK_FALSE (f.model.getLaneController().has_value());

        for (int i = 0; i < numProperties - 1; ++i)
            f.model.cycleLaneProperty (1);

        CHECK_FALSE (f.model.getLaneController().has_value());
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneController() == Controller::pitchBend);
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneController() == Controller::modWheel);
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneController() == Controller::sustain);
        f.model.cycleLaneProperty (1);
        CHECK_FALSE (f.model.getLaneController().has_value());
        CHECK (f.model.getLaneProperty() == core::note_property::getAll().front());

        // Shift+F back: from the first note value to the pedal.
        f.model.cycleLaneProperty (-1);
        CHECK (f.model.getLaneController() == Controller::sustain);

        // Choosing a note value leaves the controllers.
        f.model.setLaneProperty (core::NoteProperty::pan);
        CHECK_FALSE (f.model.getLaneController().has_value());
    }

    TEST_CASE ("drawing in the lane writes the controller where the song is; one gesture is one undo step")
    {
        LaneFixture f;
        f.model.setLaneController (Controller::modWheel);

        f.model.beginGesture ("Draw mod wheel");
        f.model.drawControllerLine (5.0, 0.0f, 5.5, 0.5f);
        f.model.drawControllerLine (5.5, 0.5f, 6.0, 1.0f);

        // In the clip: from the start of its content (beat 4 of the song).
        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 2.0) == doctest::Approx (1.0f));
        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 1.25) == doctest::Approx (0.25f).epsilon (0.03));

        const auto points = f.model.getControllerPoints (Controller::modWheel);
        REQUIRE_FALSE (points.empty());
        CHECK (points.front().beat > 5.0);
        CHECK (points.back().beat == doctest::Approx (6.0));
        CHECK (f.model.getControllerValueAt (Controller::modWheel, 7.0) == doctest::Approx (1.0f));
        CHECK (f.model.getControllerValueAt (Controller::modWheel, 4.5) == 0.0f);

        // Before the clip: nothing is written outside it.
        f.model.drawControllerLine (0.0, 1.0f, 3.0, 1.0f);
        CHECK (controllers::getPoints (*f.clip, Controller::modWheel).front().beat >= 0.0);

        f.project->undo();
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::modWheel));
    }

    TEST_CASE ("the lane draws with the mouse; Alt puts the bend back in the middle")
    {
        LaneFixture f;
        pianoroll::View view;
        view.leftBeat = 4.0;
        view.pixelsPerBeat = 40.0;
        view.width = 640.0f;
        pianoroll::ControlLane lane (f.model, view);
        lane.setBounds (0, 0, 640, 110);
        f.model.setLaneController (Controller::pitchBend);

        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        lane.mouseDown (mouseAt (lane, { view.beatToX (5.0), 6.0f }, left));          // the top: bent all the way up
        lane.mouseDrag (mouseAt (lane, { view.beatToX (6.0), 6.0f }, left));
        lane.mouseUp (mouseAt (lane, { view.beatToX (6.0), 6.0f }, juce::ModifierKeys()));
        CHECK (f.model.getControllerValueAt (Controller::pitchBend, 5.5) == doctest::Approx (1.0f));
        CHECK (f.project->getUndoDescription() == "Draw pitch bend");

        const juce::ModifierKeys altLeft (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
        lane.mouseDown (mouseAt (lane, { view.beatToX (5.5), 100.0f }, altLeft));
        lane.mouseDrag (mouseAt (lane, { view.beatToX (6.5), 100.0f }, altLeft));
        CHECK (f.model.getControllerValueAt (Controller::pitchBend, 5.75) == doctest::Approx (0.5f).epsilon (0.001));
        CHECK (f.model.getControllerValueAt (Controller::pitchBend, 5.25) == doctest::Approx (1.0f));

        // It paints the curve (what is drawn reaches the image when the drawing ends).
        juce::Image image (juce::Image::ARGB, 640, 110, true);
        {
            juce::Graphics g (image);
            lane.paint (g);
        }
        CHECK (image.getPixelAt ((int) view.beatToX (5.25), 20) != image.getPixelAt ((int) view.beatToX (5.25), 100));
    }

    TEST_CASE ("the right button erases the drawn line; the lane box lists the controllers")
    {
        LaneFixture f;
        pianoroll::View view;
        view.leftBeat = 4.0;
        view.pixelsPerBeat = 40.0;
        view.width = 640.0f;
        pianoroll::ControlLane lane (f.model, view);
        lane.setBounds (0, 0, 640, 110);
        f.model.setLaneController (Controller::modWheel);

        f.model.beginGesture ("Draw mod wheel");
        f.model.drawControllerLine (4.0, 0.0f, 12.0, 1.0f);
        const auto drawn = controllers::getPoints (*f.clip, Controller::modWheel).size();

        const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
        lane.mouseDown (mouseAt (lane, { view.beatToX (6.0), 50.0f }, right));
        lane.mouseDrag (mouseAt (lane, { view.beatToX (8.0), 50.0f }, right));
        lane.mouseUp (mouseAt (lane, { view.beatToX (8.0), 50.0f }, juce::ModifierKeys()));
        CHECK (f.project->getUndoDescription() == "Erase mod wheel");

        for (const auto& p : f.model.getControllerPoints (Controller::modWheel))
            CHECK ((p.beat < 6.0 || p.beat >= 8.0));

        CHECK (controllers::getPoints (*f.clip, Controller::modWheel).size() < drawn);
        CHECK (f.model.getControllerValueAt (Controller::modWheel, 7.0) == doctest::Approx (0.25f).epsilon (0.03));

        f.project->undo();
        CHECK (controllers::getPoints (*f.clip, Controller::modWheel).size() == drawn);
    }

    TEST_CASE ("a looping clip records the wheels into its loop")
    {
        LaneFixture f;
        core::clips::setEnd (*f.clip, 8.0, 0.25);       // one bar, beats 4..8
        core::clips::setLooping (*f.clip, true);
        core::clips::setEnd (*f.clip, 16.0, 0.25);      // three times round
        f.model.setClip (f.clip);
        f.clock.loop = { 0.0, 64.0 };
        f.input->setRecordArmed (true);
        f.clock.playing = true;
        f.clock.beat = 4.0;
        f.input->update();
        REQUIRE (f.input->isRecording());

        f.clock.beat = 9.0;     // second time round: beat 5 of the loop
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 1, 127), NoteInput::now());
        f.clock.playing = false;
        f.input->update();

        const auto mod = f.model.getControllerPoints (Controller::modWheel);
        REQUIRE (mod.size() == 1);
        CHECK (mod[0].beat == doctest::Approx (5.0));
    }

    TEST_CASE ("bend and mod wheel from a MIDI keyboard are recorded into the clip; the pedal makes notes longer instead")
    {
        LaneFixture f;
        f.clock.loop = { 0.0, 64.0 };
        f.input->setRecordArmed (true);
        f.clock.playing = true;
        f.clock.beat = 4.0;
        f.input->update();
        REQUIRE (f.input->isRecording());

        f.clock.beat = 5.0;
        f.input->handleMidiMessage (juce::MidiMessage::pitchWheel (1, 16383), NoteInput::now());
        f.clock.beat = 5.5;
        f.input->handleMidiMessage (juce::MidiMessage::pitchWheel (1, 8192), NoteInput::now());
        f.clock.beat = 6.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 1, 127), NoteInput::now());
        f.clock.beat = 6.5;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 64, 127), NoteInput::now());

        // Played outside the clip: not the clip's.
        f.clock.beat = 30.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 1, 10), NoteInput::now());

        f.clock.playing = false;
        f.input->update();
        CHECK_FALSE (f.input->isRecording());

        const auto bend = f.model.getControllerPoints (Controller::pitchBend);
        REQUIRE (bend.size() == 2);
        CHECK (bend[0].beat == doctest::Approx (5.0));
        CHECK (bend[0].value == doctest::Approx (1.0f));
        CHECK (bend[1].value == doctest::Approx (0.5f).epsilon (0.001));

        const auto mod = f.model.getControllerPoints (Controller::modWheel);
        REQUIRE (mod.size() == 1);
        CHECK (mod[0] == controllers::Point { 6.0, 1.0f });

        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::sustain));

        // The take is one undo step.
        f.project->undo();
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::pitchBend));
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::modWheel));
    }
}
