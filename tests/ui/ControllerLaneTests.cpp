#include <doctest.h>

#include "core/Clips.h"
#include "core/Controllers.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"
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
    TEST_CASE ("F goes through the note values, then the common controllers and the others the clip has")
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
        CHECK (f.model.getLaneController() == Controller::expression);
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneController() == Controller::volume);
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneController() == Controller::pan);
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneController() == Controller::aftertouch);
        f.model.cycleLaneProperty (1);
        CHECK_FALSE (f.model.getLaneController().has_value());
        CHECK (f.model.getLaneProperty() == core::note_property::getAll().front());

        // Shift+F back: from the first note value to the last controller.
        f.model.cycleLaneProperty (-1);
        CHECK (f.model.getLaneController() == Controller::aftertouch);

        // A knob the clip has events of joins the round, after the common ones.
        CHECK (f.model.getLaneControllers().size() == controllers::getCommon().size());
        controllers::add (*f.clip, Controller::cc (74), 1.0, 0.5f);
        CHECK (f.model.getLaneControllers().back() == Controller::cc (74));
        f.model.cycleLaneProperty (1);
        CHECK (f.model.getLaneController() == Controller::cc (74));
        f.model.cycleLaneProperty (1);
        CHECK_FALSE (f.model.getLaneController().has_value());

        // One chosen from "Other controllers" that the clip has nothing of: F goes on to the note
        // values, Shift+F back to the last controller of the round.
        f.model.setLaneController (Controller::cc (21));
        f.model.cycleLaneProperty (1);
        CHECK_FALSE (f.model.getLaneController().has_value());
        CHECK (f.model.getLaneProperty() == core::note_property::getAll().front());
        f.model.setLaneController (Controller::cc (21));
        f.model.cycleLaneProperty (-1);
        CHECK (f.model.getLaneController() == Controller::cc (74));

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

    TEST_CASE ("the list above the lower panel: the common controllers, those the clip has, the rest under Other controllers")
    {
        using pianoroll::PianoRoll;
        auto project = core::Project::createEmpty (test::session());
        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 16.0, "Main");
        REQUIRE (clip != nullptr);
        project->clearUndoHistory();
        ui::MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto& model = main.getPianoRollModel();
        REQUIRE (model.getClip() == clip);
        auto& box = main.getPianoRoll().getLaneBox();
        const auto mark = juce::String::fromUTF8 ("  \xe2\x80\xa2");

        const auto letMessagesThrough = []
        {
            for (int i = 0; i < 5; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        };

        // The controllers in the list itself, and those one level down.
        struct Listed
        {
            juce::StringArray top, others;
        };

        const auto read = [&box]
        {
            Listed listed;

            for (juce::PopupMenu::MenuItemIterator it (*box.getRootMenu()); it.next();)
            {
                const auto& item = it.getItem();

                if (item.subMenu != nullptr)
                {
                    CHECK (item.text == "Other controllers");

                    for (juce::PopupMenu::MenuItemIterator sub (*item.subMenu); sub.next();)
                        listed.others.add (sub.getItem().text);
                }
                else if (item.itemID >= PianoRoll::getLaneBoxId (Controller::pitchBend))
                {
                    listed.top.add (item.text);
                }
            }

            return listed;
        };

        auto listed = read();
        CHECK (listed.top == juce::StringArray ("Pitch bend", "Mod wheel (CC 1)", "Sustain pedal (CC 64)", "Expression (CC 11)",
                                                "Volume (CC 7)", "Pan (CC 10)", "Aftertouch"));
        CHECK (listed.others.size() == (int) (controllers::getAll().size() - controllers::getCommon().size()));
        CHECK (listed.others.contains ("CC 74 Brightness"));
        CHECK (listed.others.contains ("CC 21"));

        // Choosing one under "Other controllers" shows it in the lower panel.
        box.setSelectedId (PianoRoll::getLaneBoxId (Controller::cc (74)), juce::sendNotificationSync);
        CHECK (model.getLaneController() == Controller::cc (74));
        CHECK (box.getText() == "CC 74 Brightness");

        // Drawn: the clip has it now, so it is in the list itself, with a mark.
        model.beginGesture ("Draw cc 74 brightness");
        model.drawControllerLine (0.0, 0.0f, 2.0, 1.0f);
        letMessagesThrough();
        listed = read();
        CHECK (listed.top.contains ("CC 74 Brightness" + mark));
        CHECK_FALSE (listed.others.contains ("CC 74 Brightness"));
        CHECK (box.getSelectedId() == PianoRoll::getLaneBoxId (Controller::cc (74)));
        CHECK (box.getText() == "CC 74 Brightness" + mark);

        // A common one drawn is marked where it stands.
        model.setLaneController (Controller::expression);
        model.beginGesture ("Draw expression");
        model.drawControllerLine (0.0, 1.0f, 2.0, 0.0f);
        letMessagesThrough();
        listed = read();
        CHECK (listed.top[3] == "Expression (CC 11)" + mark);
        CHECK (box.getText() == "Expression (CC 11)" + mark);

        // Undo both: nothing is marked, CC 74 is back under "Other controllers".
        project->undo();
        project->undo();
        letMessagesThrough();
        CHECK_FALSE (controllers::hasPoints (*clip, Controller::cc (74)));
        listed = read();
        CHECK (listed.top.size() == 7);
        CHECK (listed.top[3] == "Expression (CC 11)");
        CHECK (listed.others.contains ("CC 74 Brightness"));

        // A note value again.
        box.setSelectedId (1, juce::sendNotificationSync);
        CHECK_FALSE (model.getLaneController().has_value());

        // The window paints with a controller of "Other controllers" shown.
        model.setLaneController (Controller::cc (21));
        letMessagesThrough();
        CHECK (box.getText() == "CC 21");
        juce::Image image (juce::Image::RGB, 1280, 800, true);
        juce::Graphics g (image);
        main.paintEntireComponent (g, true);
    }

    TEST_CASE ("expression, a knob and aftertouch from a MIDI keyboard are recorded into the clip; \"all notes off\" isn't")
    {
        LaneFixture f;
        f.clock.loop = { 0.0, 64.0 };
        f.input->setRecordArmed (true);
        f.clock.playing = true;
        f.clock.beat = 4.0;
        f.input->update();
        REQUIRE (f.input->isRecording());

        f.clock.beat = 5.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 11, 64), NoteInput::now());
        f.clock.beat = 6.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 21, 127), NoteInput::now());
        f.clock.beat = 7.0;
        f.input->handleMidiMessage (juce::MidiMessage::channelPressureChange (1, 127), NoteInput::now());
        f.clock.beat = 7.5;
        f.input->handleMidiMessage (juce::MidiMessage::channelPressureChange (1, 0), NoteInput::now());
        f.clock.beat = 8.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 123, 0), NoteInput::now());
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 0, 3), NoteInput::now());

        f.clock.playing = false;
        f.input->update();
        CHECK_FALSE (f.input->isRecording());

        const auto expression = f.model.getControllerPoints (Controller::expression);
        REQUIRE (expression.size() == 1);
        CHECK (expression[0].beat == doctest::Approx (5.0));
        CHECK (expression[0].value == doctest::Approx (64.0f / 127.0f));

        const auto knob = f.model.getControllerPoints (Controller::cc (21));
        REQUIRE (knob.size() == 1);
        CHECK (knob[0] == controllers::Point { 6.0, 1.0f });

        const auto pressure = f.model.getControllerPoints (Controller::aftertouch);
        REQUIRE (pressure.size() == 2);
        CHECK (pressure[0] == controllers::Point { 7.0, 1.0f });
        CHECK (pressure[1] == controllers::Point { 7.5, 0.0f });

        const std::vector<Controller> expected { Controller::expression, Controller::aftertouch, Controller::cc (21) };
        CHECK (controllers::getUsed (*f.clip) == expected);

        // The knob is in F's round now.
        CHECK (f.model.getLaneControllers().back() == Controller::cc (21));

        // The take is one undo step.
        f.project->undo();
        CHECK (controllers::getUsed (*f.clip).empty());
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
