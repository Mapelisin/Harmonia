#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Automation.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Sends.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/TrackHeaders.h"

using namespace harmonia;
using namespace harmonia::ui::arrangement;
namespace clips = core::clips;
namespace instruments = audio::instruments;
namespace sends = audio::sends;

namespace
{
    void dispatch()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    core::Note makeNote (int pitch, double start, double length)
    {
        core::Note note;
        note.pitch = pitch;
        note.startBeat = start;
        note.lengthBeats = length;
        note.velocity = 110;
        return note;
    }

    int countNotes (te::Clip* clip)
    {
        auto* midi = dynamic_cast<te::MidiClip*> (clip);
        return midi != nullptr ? midi->getSequence().getNotes().size() : -1;
    }

    juce::StringArray getNames (ArrangementModel& model)
    {
        juce::StringArray names;

        for (auto* track : model.getTracks())
            names.add (track->getName());

        return names;
    }

    float getRmsDb (const juce::File& file, double from, double to)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        const auto start = (juce::int64) (from * reader->sampleRate);
        const auto length = (int) ((to - from) * reader->sampleRate);
        juce::AudioBuffer<float> buffer (2, length);
        reader->read (&buffer, 0, length, start, true, true);
        return juce::Decibels::gainToDecibels ((buffer.getRMSLevel (0, 0, length) + buffer.getRMSLevel (1, 0, length)) / 2.0f);
    }

    /** Three named tracks A, B, C (then a reverb return). */
    struct ThreeTracks
    {
        ThreeTracks()
        {
            project->getTracks().getFirst()->setName ("A");
            project->addTrack ("B");
            project->addTrack ("C");
            juce::String error;
            reverb = sends::addReturnTrack (*project, sends::Preset::reverb, error);
            REQUIRE (reverb != nullptr);
            project->clearUndoHistory();
            dispatch();
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        ArrangementModel model { *project };
        te::AudioTrack* reverb = nullptr;
    };
}

TEST_SUITE ("arrangement: track commands")
{
    TEST_CASE ("a duplicated track is right below with its instrument, effects, settings, automation, sends and clips of its own")
    {
        auto project = core::Project::createEmpty (test::session());
        ArrangementModel model { *project };
        auto& lead = *project->getTracks().getFirst();
        lead.setName ("Lead");
        REQUIRE (instruments::useBuiltInSynth (lead).isEmpty());

        juce::String error;
        const auto builtIn = audio::effects::getBuiltIn();
        REQUIRE (audio::effects::add (lead.pluginList, builtIn[1], error) != nullptr);
        lead.getVolumePlugin()->setVolumeDb (-6.0f);
        auto* volume = audio::automation::getParameters (lead)[0];
        audio::automation::addPoint (*volume, 0.0, 0.5f);
        audio::automation::addPoint (*volume, 4.0, 0.8f);
        auto* reverb = sends::addReturnTrack (*project, sends::Preset::reverb, error);
        REQUIRE (reverb != nullptr);
        REQUIRE (sends::setLevelDb (lead, *reverb, -10.0f));

        auto* first = project->addMidiClip (lead, 0.0, 4.0, "Lead");
        core::NoteEditor (*first).add ({ makeNote (60, 0.0, 1.0), makeNote (64, 1.0, 1.0) });
        auto* linked = core::clip_links::duplicate (*first, 8.0, true);
        REQUIRE (linked != nullptr);
        project->addTrack ("Bass");
        project->clearUndoHistory();
        dispatch();

        auto* copy = model.duplicateTrack (lead);
        dispatch();
        REQUIRE (copy != nullptr);
        CHECK (getNames (model) == juce::StringArray { "Lead", "Lead 2", "Bass" });
        CHECK (model.getCurrentTrack() == copy);

        CHECK (instruments::getName (*copy) == instruments::getName (lead));
        CHECK (audio::effects::get (copy->pluginList).size() == 1);
        CHECK (copy->getVolumePlugin()->volParam->getCurrentExplicitValue() == doctest::Approx (lead.getVolumePlugin()->volParam->getCurrentExplicitValue()));
        CHECK (audio::automation::getPoints (*audio::automation::getParameters (*copy)[0]).size() == 2);
        CHECK (sends::getLevelDb (*copy, *reverb) == doctest::Approx (-10.0f).epsilon (0.01));

        const auto copied = clips::getMidiClips (*copy);
        REQUIRE (copied.size() == 2);
        CHECK (copied[0]->getName() == "Lead 2");
        CHECK (countNotes (copied[0]) == 2);
        CHECK (countNotes (copied[1]) == 2);
        CHECK_FALSE (core::clip_links::isLinked (*copied[0]));
        CHECK_FALSE (core::clip_links::isLinked (*copied[1]));
        CHECK (core::clip_links::isLinked (*first));   // the originals still are

        // The copy's notes are its own.
        project->beginGesture ("Add note");
        core::NoteEditor (*copied[0]).add (makeNote (67, 2.0, 1.0));
        CHECK (countNotes (copied[0]) == 3);
        CHECK (countNotes (first) == 2);
        CHECK (countNotes (linked) == 2);

        // Each a step to undo.
        project->undo();
        project->undo();
        dispatch();
        CHECK (getNames (model) == juce::StringArray { "Lead", "Bass" });
        CHECK (core::clip_links::isLinked (*first));
    }

    TEST_CASE ("names for copies")
    {
        ThreeTracks t;
        CHECK (t.model.makeCopyName ("Bass") == "Bass 2");
        CHECK (t.model.makeCopyName ("Track 9") == "Track 10");
        CHECK (t.model.makeCopyName ("  ") == "Track 2");

        t.model.getTrack (1)->setName ("A 2");
        CHECK (t.model.makeCopyName ("A") == "A 3");
        CHECK (t.model.makeCopyName ("A 2") == "A 3");
    }

    TEST_CASE ("tracks move up and down, one undo step each; the return tracks stay at the end")
    {
        ThreeTracks t;
        t.model.moveTrack (*t.model.getTrack (2), 0);
        dispatch();
        CHECK (getNames (t.model) == juce::StringArray { "C", "A", "B" });
        CHECK (te::getAudioTracks (t.project->getEdit()).getLast() == t.reverb);

        t.model.moveTrack (*t.model.getTrack (1), 2);
        dispatch();
        CHECK (getNames (t.model) == juce::StringArray { "C", "B", "A" });

        t.model.moveTrack (*t.model.getTrack (2), 7);   // already last
        t.project->undo();
        dispatch();
        CHECK (getNames (t.model) == juce::StringArray { "C", "A", "B" });
        t.project->undo();
        dispatch();
        CHECK (getNames (t.model) == juce::StringArray { "A", "B", "C" });
    }

    TEST_CASE ("dragging a header moves its track; a line shows where it goes")
    {
        ThreeTracks t;
        ArrangementView view;
        view.numTracks = 3;
        TrackHeaders headers (t.model, view);
        headers.setSize (200, 300);
        const auto row = ArrangementView::trackHeight;

        headers.dragHeader (0, row * 2.0f + 10.0f);
        CHECK (headers.getDropGap() == 2);
        CHECK (headers.createComponentSnapshot (headers.getLocalBounds()).isValid());

        headers.dragHeader (0, 10.0f);   // where it is: nowhere to go
        CHECK_FALSE (headers.getDropGap().has_value());

        headers.endHeaderDrag (0, row * 3.0f - 5.0f);
        dispatch();
        CHECK_FALSE (headers.getDropGap().has_value());
        CHECK (getNames (t.model) == juce::StringArray { "B", "C", "A" });

        headers.endHeaderDrag (2, row * 0.2f);
        dispatch();
        CHECK (getNames (t.model) == juce::StringArray { "A", "B", "C" });
    }

    TEST_CASE ("render to audio: a new audio track sounding as the track did, which is muted; one undo step")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        ArrangementModel model { *project };
        auto& lead = *project->getTracks().getFirst();
        lead.setName ("Lead");
        REQUIRE (instruments::useBuiltInSynth (lead).isEmpty());
        auto* clip = project->addMidiClip (lead, 4.0, 4.0, "Lead");
        core::NoteEditor (*clip).add ({ makeNote (60, 0.0, 2.0), makeNote (67, 0.0, 2.0) });   // 2–3 s

        juce::String error;
        auto* reverb = sends::addReturnTrack (*project, sends::Preset::reverb, error);
        REQUIRE (reverb != nullptr);
        REQUIRE (sends::setLevelDb (lead, *reverb, -6.0f));
        auto& bass = *project->addTrack ("Bass");
        dispatch();

        REQUIRE (project->save (temp.getFile ("Song.harmonia")).isEmpty());
        const auto before = temp.getFile ("Before.wav");
        REQUIRE (audio::exportWav (project->getEdit(), before).isEmpty());
        dispatch();

        // As between two things the user does: Tracktion starts a new undo step 350 ms after
        // the last change (its clip sorting after the render then joins the render's step).
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

        REQUIRE (model.renderToAudio (lead).isEmpty());
        dispatch();

        REQUIRE (getNames (model) == juce::StringArray { "Lead", "Lead (audio)", "Bass" });
        auto* audioTrack = model.getTrack (1);
        CHECK (core::tracks::isAudio (*audioTrack));
        CHECK (lead.isMuted (false));
        CHECK (model.getCurrentTrack() == audioTrack);
        CHECK (sends::getLevelDb (*audioTrack, *reverb) == doctest::Approx (-6.0f).epsilon (0.01));

        const auto onTrack = clips::getClips (*audioTrack);
        REQUIRE (onTrack.size() == 1);
        CHECK (clips::getStartBeat (*onTrack[0]) == doctest::Approx (4.0));
        CHECK (model.isSelected (*onTrack[0]));
        const auto file = dynamic_cast<te::AudioClipBase*> (onTrack[0])->getCurrentSourceFile();
        CHECK (file.existsAsFile());
        CHECK (file.getParentDirectory() == temp.getFile ("Song Audio"));
        CHECK (getRmsDb (file, 0.0, 1.0) > -40.0f);

        // The song sounds as it did (the synth's random phases make it differ a little).
        const auto after = temp.getFile ("After.wav");
        REQUIRE (audio::exportWav (project->getEdit(), after).isEmpty());
        CHECK (std::abs (getRmsDb (after, 2.0, 3.0) - getRmsDb (before, 2.0, 3.0)) < 1.5f);
        CHECK (std::abs (getRmsDb (after, 3.2, 3.8) - getRmsDb (before, 3.2, 3.8)) < 3.0f);   // the reverb's tail

        project->undo();
        dispatch();
        CHECK (getNames (model) == juce::StringArray { "Lead", "Bass" });
        CHECK_FALSE (lead.isMuted (false));
        CHECK (file.existsAsFile());

        CHECK (model.renderToAudio (bass).startsWith ("Nothing to render"));
    }

    TEST_CASE ("a track renders even when muted or another is soloed, and is left as it was")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& lead = *project->getTracks().getFirst();
        REQUIRE (instruments::useBuiltInSynth (lead).isEmpty());
        core::NoteEditor (*project->addMidiClip (lead, 0.0, 4.0)).add ({ makeNote (60, 0.0, 2.0) });
        auto& other = *project->addTrack ("Other");
        lead.setMute (true);
        other.setSolo (true);
        project->clearUndoHistory();

        const auto file = temp.getFile ("Lead.wav");
        REQUIRE (audio::renderTrack (lead, file, { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }).isEmpty());
        CHECK (getRmsDb (file, 0.0, 0.9) > -40.0f);

        CHECK (lead.isMuted (false));
        CHECK (other.isSolo (false));
        CHECK_FALSE (lead.isSoloIsolate (false));
        CHECK_FALSE (project->hasUnsavedChanges());
        CHECK_FALSE (project->canUndo());
    }
}
