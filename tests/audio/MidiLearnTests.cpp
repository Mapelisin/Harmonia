#include "sandbox/TestPluginSong.h"

#include "TestAudioRig.h"
#include "audio/Automation.h"
#include "audio/AutomationWriter.h"
#include "audio/Effects.h"
#include "audio/MidiLearn.h"
#include "audio/Transport.h"

using namespace harmonia;
namespace learn = audio::midi_learn;
namespace automation = audio::automation;

namespace
{
    juce::MidiMessage cc (int number, int value, int channel = 1)
    {
        return juce::MidiMessage::controllerEvent (channel, number, value);
    }

    /** Lets messages through for a while. */
    void pump (int ms)
    {
        for (const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) ms; juce::Time::getMillisecondCounter() < until;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    /** Lets messages through until `condition` holds (or the time is up): what it says then. */
    template <typename Condition>
    bool waitFor (Condition&& condition, int timeoutMs = 5000)
    {
        for (const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
             ! condition() && juce::Time::getMillisecondCounter() < until;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        return condition();
    }

    /** Long enough for a control's turn to end. */
    void rest()
    {
        pump ((int) learn::Controller::restMs + 200);
    }

    te::AutomatableParameter& volumeOf (te::AudioTrack& track)    { return *track.getVolumePlugin()->volParam; }
    te::AutomatableParameter& panOf (te::AudioTrack& track)       { return *track.getVolumePlugin()->panParam; }
}

TEST_SUITE ("midi learn")
{
    TEST_CASE ("a control is a control change on a channel; bank select and the mode messages aren't")
    {
        const auto control = learn::Control::from (cc (21, 64, 2));
        CHECK (control.isValid());
        CHECK (control.channel == 2);
        CHECK (control.number == 21);
        CHECK (control.describe() == "CC 21 (channel 2)");

        CHECK (learn::Control::from (cc (1, 10)).isValid());           // the modulation wheel can move a setting
        CHECK_FALSE (learn::Control::from (cc (0, 1)).isValid());       // bank select
        CHECK_FALSE (learn::Control::from (cc (32, 1)).isValid());
        CHECK_FALSE (learn::Control::from (cc (6, 1)).isValid());       // data entry
        CHECK_FALSE (learn::Control::from (cc (99, 1)).isValid());      // NRPN
        CHECK_FALSE (learn::Control::from (cc (123, 0)).isValid());     // all notes off
        CHECK_FALSE (learn::Control::from (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)).isValid());
        CHECK_FALSE (learn::Control::from (juce::MidiMessage::pitchWheel (1, 9000)).isValid());
    }

    TEST_CASE ("assigning: one control per setting and one setting per control, each change one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        auto& volume = volumeOf (track);
        auto& pan = panOf (track);
        project->clearUndoHistory();

        CHECK (learn::canAssign (volume));
        CHECK (learn::describe (volume) == track.getName() + ": Volume");
        CHECK (learn::getAll (project->getEdit()).empty());
        CHECK_FALSE (learn::getControl (volume).isValid());

        const learn::Control knob1 { 1, 21 }, knob2 { 1, 22 };
        learn::assign (volume, knob1);
        CHECK (learn::getControl (volume) == knob1);
        CHECK (learn::getParameter (project->getEdit(), knob1).get() == &volume);
        CHECK (project->getUndoDescription() == "MIDI control for " + track.getName() + ": Volume");
        CHECK (project->hasUnsavedChanges());

        // The same control on another setting: it moves that one now, not both.
        learn::assign (pan, knob1);
        CHECK (learn::getControl (pan) == knob1);
        CHECK_FALSE (learn::getControl (volume).isValid());
        CHECK (learn::getAll (project->getEdit()).size() == 1);

        // Another control on the same setting: it replaces the first.
        learn::assign (pan, knob2);
        CHECK (learn::getControl (pan) == knob2);
        CHECK (learn::getParameter (project->getEdit(), knob1) == nullptr);
        CHECK (learn::getAll (project->getEdit()).size() == 1);

        learn::assign (volume, knob1);
        CHECK (learn::getAll (project->getEdit()).size() == 2);

        learn::forget (pan);
        CHECK_FALSE (learn::getControl (pan).isValid());
        CHECK (learn::getControl (volume) == knob1);

        project->undo();     // the forget
        CHECK (learn::getControl (pan) == knob2);
        project->undo();     // volume on knob 1
        CHECK_FALSE (learn::getControl (volume).isValid());
        project->undo();     // pan from knob 1 to knob 2
        CHECK (learn::getControl (pan) == knob1);
        project->redo();
        CHECK (learn::getControl (pan) == knob2);

        learn::assign (volume, knob1);
        learn::forgetAll (project->getEdit());
        CHECK (learn::getAll (project->getEdit()).empty());
        CHECK (project->getUndoDescription() == "Forget all MIDI controls");
        project->undo();
        CHECK (learn::getAll (project->getEdit()).size() == 2);
    }

    TEST_CASE ("the assignments are saved with the song, those of a deleted effect left out")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();

        juce::String error;
        auto* eq = audio::effects::add (track.pluginList, audio::effects::getBuiltIn().getFirst(), error);
        REQUIRE_MESSAGE (eq != nullptr, error.toStdString());
        REQUIRE (eq->getNumAutomatableParameters() > 0);
        auto& knob = *eq->getAutomatableParameter (0);

        learn::assign (volumeOf (track), { 3, 7 });
        learn::assign (knob, { 1, 74 });
        learn::assign (*project->getEdit().getMasterVolumePlugin()->volParam, { 1, 80 });
        CHECK (learn::describe (*project->getEdit().getMasterVolumePlugin()->volParam) == "Master: Volume");

        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());

        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE (loaded != nullptr);
        auto& loadedTrack = *loaded->getTracks().getFirst();
        auto& loadedEdit = loaded->getEdit();
        CHECK (learn::getControl (volumeOf (loadedTrack)) == learn::Control { 3, 7 });
        CHECK (learn::getControl (*loadedEdit.getMasterVolumePlugin()->volParam) == learn::Control { 1, 80 });

        auto loadedKnob = learn::getParameter (loadedEdit, { 1, 74 });
        REQUIRE (loadedKnob != nullptr);
        CHECK (loadedKnob->paramID == knob.paramID);
        CHECK (learn::getAll (loadedEdit).size() == 3);

        // The effect deleted: its assignment no longer counts.
        loadedKnob->getPlugin()->deleteFromParent();
        loadedKnob = nullptr;
        CHECK (learn::getAll (loadedEdit).size() == 2);
        CHECK (learn::getParameter (loadedEdit, { 1, 74 }) == nullptr);
    }

    TEST_CASE ("a control moves its setting; each turn of it is one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();
        auto& track = *project->getTracks().getFirst();
        auto& volume = volumeOf (track);
        learn::Controller controller (edit);
        CHECK (learn::Controller::find (edit) == &controller);

        learn::assign (volume, { 1, 21 });
        project->clearUndoHistory();
        const auto before = volume.getCurrentValue();

        CHECK_FALSE (controller.handle (cc (22, 64)));      // nothing assigned to it
        CHECK_FALSE (controller.handle (cc (21, 64, 2)));   // another channel
        CHECK_FALSE (controller.handle (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)));

        // A turn up to the top.
        for (const auto value : { 10, 60, 100, 127 })
            CHECK (controller.handle (cc (21, value)));

        CHECK (volume.getCurrentValue() == doctest::Approx (1.0f));
        rest();

        // A turn down to the bottom.
        controller.handle (cc (21, 40));
        controller.handle (cc (21, 0));
        CHECK (volume.getCurrentValue() == doctest::Approx (0.0f));
        rest();

        CHECK (project->getUndoDescription() == "MIDI: " + track.getName() + ": Volume");
        project->undo();
        CHECK (volume.getCurrentValue() == doctest::Approx (1.0f));
        project->undo();
        CHECK (volume.getCurrentValue() == doctest::Approx (before));
        CHECK_FALSE (project->canUndo());
    }

    TEST_CASE ("learning one setting: the next control moved is assigned to it, and doesn't move it")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();
        auto& track = *project->getTracks().getFirst();
        auto& pan = panOf (track);
        learn::Controller controller (edit);

        juce::StringArray messages;
        int changes = 0;
        controller.onMessage = [&] (const juce::String& text) { messages.add (text); };
        controller.onLearningChanged = [&] { ++changes; };

        controller.learn (pan);
        CHECK (controller.isLearning());
        CHECK (controller.getWaitingParameter() == &pan);
        CHECK (messages[messages.size() - 1].contains (track.getName() + ": Pan"));

        // Notes still play while waiting; the control is what's learnt.
        CHECK_FALSE (controller.handle (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)));
        CHECK (controller.handle (cc (74, 127, 5)));
        CHECK_FALSE (controller.isLearning());
        CHECK (controller.getWaitingParameter() == nullptr);
        CHECK (learn::getControl (pan) == learn::Control { 5, 74 });
        CHECK (pan.getCurrentValue() == doctest::Approx (0.0f));
        CHECK (messages[messages.size() - 1] == "CC 74 (channel 5) moves " + track.getName() + ": Pan");
        CHECK (changes == 2);

        // From now on it moves it.
        controller.handle (cc (74, 0, 5));
        CHECK (pan.getCurrentValue() == doctest::Approx (-1.0f));

        // Cancelled.
        controller.learn (volumeOf (track));
        controller.stopLearning();
        CHECK_FALSE (controller.isLearning());
        CHECK_FALSE (learn::getControl (volumeOf (track)).isValid());
    }

    TEST_CASE ("MIDI Learn for whatever is moved next: one setting after another, until stopped")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();
        auto& track = *project->getTracks().getFirst();
        auto& volume = volumeOf (track);
        auto& pan = panOf (track);
        learn::Controller controller (edit);

        controller.startLearning();
        CHECK (controller.isLearning());
        CHECK (controller.getWaitingParameter() == nullptr);
        CHECK (controller.handle (cc (21, 64)));        // nothing waits: swallowed, not assigned
        CHECK (learn::getAll (edit).empty());

        // A fader moved (as the mixer moves it), then a control.
        volume.setParameter (0.3f, juce::sendNotification);
        CHECK (controller.getWaitingParameter() == &volume);
        CHECK (controller.handle (cc (21, 64)));
        CHECK (learn::getControl (volume) == learn::Control { 1, 21 });
        CHECK (controller.isLearning());

        pan.setParameter (0.5f, juce::sendNotification);
        CHECK (controller.getWaitingParameter() == &pan);
        controller.handle (cc (22, 1));
        CHECK (learn::getControl (pan) == learn::Control { 1, 22 });

        // Stopped: a setting moved doesn't wait for a control any more.
        controller.stopLearning();
        CHECK_FALSE (controller.isLearning());
        volume.setParameter (0.6f, juce::sendNotification);
        CHECK (controller.getWaitingParameter() == nullptr);
        CHECK_FALSE (controller.handle (cc (23, 64)));

        // Off, the controls move their settings again.
        CHECK (controller.handle (cc (21, 127)));
        CHECK (volume.getCurrentValue() == doctest::Approx (1.0f));
        rest();
    }

    TEST_CASE ("a knob of a plugin in a process of its own: learnt from its window, moved by a control")
    {
        test::plugins::SynthSong song;
        auto& edit = song.project->getEdit();
        auto& level = song.parameter (test::plugins::level);
        learn::Controller controller (edit);

        controller.startLearning();
        level.setValueNotifyingHost (0.4f);     // turned in the plugin's own window
        REQUIRE (waitFor ([&] { return controller.getWaitingParameter() != nullptr; }));
        auto* parameter = controller.getWaitingParameter();
        CHECK (parameter->getPlugin() == &song.instrument());

        controller.handle (cc (30, 0));
        controller.stopLearning();
        CHECK (learn::getControl (*parameter) == learn::Control { 1, 30 });

        controller.handle (cc (30, 127));
        CHECK (waitFor ([&] { return std::abs (level.getValue() - 1.0f) < 1.0e-3f; }));
        controller.handle (cc (30, 0));
        CHECK (waitFor ([&] { return std::abs (level.getValue()) < 1.0e-3f; }));
        rest();
    }
}

TEST_SUITE ("automation writer")
{
    TEST_CASE ("on, a setting moved while the song plays is written into its line; let go, the line goes on as it was")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto& track = *rig.project->getTracks().getFirst();
        auto& volume = volumeOf (track);
        const auto zeroDb = te::decibelsToVolumeFaderPosition (0.0f);

        // A flat line at 0 dB over the first 16 beats.
        automation::addPoint (volume, 0.0, zeroDb);
        automation::addPoint (volume, 16.0, zeroDb);
        rig.project->clearUndoHistory();

        automation::Writer writer (edit);
        CHECK_FALSE (writer.isOn());
        CHECK ((track.automationMode.get() == te::AutomationMode::read));

        int changes = 0;
        writer.onChanged = [&] { ++changes; };
        writer.setOn (true);
        CHECK (writer.isOn());
        CHECK (changes == 1);
        CHECK ((track.automationMode.get() == te::AutomationMode::touch));
        CHECK_FALSE (rig.project->hasUnsavedChanges());     // a way of working, not a change of the song

        audio::Transport transport (edit);
        transport.play();
        rig.process (1.0, 0.0f);     // to beat 2 (120 BPM)

        // Grabbed for a second (beats 2..4), pulled down, let go.
        edit.getUndoManager().beginNewTransaction ("Write");
        automation::beginTouch (volume);

        for (int i = 0; i < 10; ++i)
        {
            volume.setParameter (0.2f, juce::sendNotification);
            rig.process (0.1, 0.0f);
            pump (5);
        }

        automation::endTouch (volume);
        rig.process (1.0, 0.0f);
        pump (50);
        transport.stop();
        pump (50);

        CHECK (automation::getValueAt (volume, 3.0) == doctest::Approx (0.2f).epsilon (0.02));
        CHECK (automation::getValueAt (volume, 1.0) == doctest::Approx (zeroDb).epsilon (0.02));
        CHECK (automation::getValueAt (volume, 8.0) == doctest::Approx (zeroDb).epsilon (0.02));

        writer.setOn (false);
        CHECK ((track.automationMode.get() == te::AutomationMode::read));
        CHECK (changes == 2);
    }

    TEST_CASE ("the master's fader moved while the song plays is written into the master's line")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto& volume = *edit.getMasterVolumePlugin()->volParam;
        const auto zeroDb = te::decibelsToVolumeFaderPosition (0.0f);

        automation::addPoint (volume, 0.0, zeroDb);
        automation::addPoint (volume, 16.0, zeroDb);
        rig.project->clearUndoHistory();

        automation::Writer writer (edit);
        writer.setOn (true);
        CHECK_FALSE (rig.project->hasUnsavedChanges());

        audio::Transport transport (edit);
        transport.play();
        rig.process (1.0, 0.0f);     // to beat 2 (120 BPM)

        edit.getUndoManager().beginNewTransaction ("Write");
        automation::beginTouch (volume);

        for (int i = 0; i < 10; ++i)
        {
            volume.setParameter (0.2f, juce::sendNotification);
            rig.process (0.1, 0.0f);
            pump (5);
        }

        automation::endTouch (volume);
        rig.process (1.0, 0.0f);
        pump (50);
        transport.stop();
        pump (50);

        CHECK (automation::getValueAt (volume, 3.0) == doctest::Approx (0.2f).epsilon (0.02));
        CHECK (automation::getValueAt (volume, 1.0) == doctest::Approx (zeroDb).epsilon (0.02));
        CHECK (automation::getValueAt (volume, 8.0) == doctest::Approx (zeroDb).epsilon (0.02));
        writer.setOn (false);
    }

    TEST_CASE ("a MIDI control's turn is written too, as one undo step")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto& track = *rig.project->getTracks().getFirst();
        auto& pan = panOf (track);
        automation::addPoint (pan, 0.0, 0.5f);
        automation::addPoint (pan, 16.0, 0.5f);
        learn::assign (pan, { 1, 10 });
        rig.project->clearUndoHistory();

        learn::Controller controller (edit);
        automation::Writer writer (edit);
        writer.setOn (true);
        audio::Transport transport (edit);
        transport.play();
        rig.process (1.0, 0.0f);

        // Turned hard right over beats 2..4, then left alone.
        for (int i = 0; i < 10; ++i)
        {
            controller.handle (cc (10, 127));
            rig.process (0.1, 0.0f);
            pump (5);
        }

        rest();
        rig.process (1.0, 0.0f);
        pump (50);
        transport.stop();
        pump (50);

        CHECK (automation::getValueAt (pan, 3.0) == doctest::Approx (1.0f).epsilon (0.02));
        CHECK (automation::getValueAt (pan, 8.0) == doctest::Approx (0.5f).epsilon (0.02));
        CHECK (automation::getPoints (pan).size() > 2);

        CHECK (rig.project->getUndoDescription() == "MIDI: " + track.getName() + ": Pan");
        rig.project->undo();
        CHECK (automation::getPoints (pan).size() == 2);
        CHECK (automation::getValueAt (pan, 3.0) == doctest::Approx (0.5f));
        CHECK_FALSE (rig.project->canUndo());
    }

    TEST_CASE ("off, nothing is written; tracks added while it's on write too")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto& track = *rig.project->getTracks().getFirst();
        auto& pan = panOf (track);

        automation::Writer writer (edit);
        audio::Transport transport (edit);
        transport.play();
        rig.process (0.5, 0.0f);

        automation::beginTouch (pan);
        pan.setParameter (0.7f, juce::sendNotification);
        rig.process (0.3, 0.0f);
        pump (150);
        automation::endTouch (pan);
        transport.stop();
        pump (50);
        CHECK_FALSE (automation::hasPoints (pan));

        writer.setOn (true);
        auto* added = rig.project->addTrack ("New");
        REQUIRE (added != nullptr);
        CHECK ((added->automationMode.get() == te::AutomationMode::touch));
        CHECK ((track.automationMode.get() == te::AutomationMode::touch));

        writer.setOn (false);
        CHECK ((added->automationMode.get() == te::AutomationMode::read));
    }

    TEST_CASE ("a setting grabbed twice at once (the mouse and a control) is let go when both have let go")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& volume = volumeOf (*project->getTracks().getFirst());

        struct Gestures final : te::AutomatableParameter::Listener
        {
            void curveHasChanged (te::AutomatableParameter&) override {}
            void parameterChangeGestureBegin (te::AutomatableParameter&) override   { ++begun; }
            void parameterChangeGestureEnd (te::AutomatableParameter&) override     { ++ended; }
            int begun = 0, ended = 0;
        } gestures;

        volume.addListener (&gestures);

        {
            automation::Touch mouse;
            mouse.begin (volume);
            mouse.begin (volume);   // already held
            automation::beginTouch (volume);
            CHECK (gestures.begun == 1);

            automation::endTouch (volume);
            CHECK (gestures.ended == 0);
            CHECK (mouse.isOn());
        }

        CHECK (gestures.ended == 1);
        volume.removeListener (&gestures);
    }
}
