#include <doctest.h>

#include "TestAudioRig.h"
#include "TestSession.h"
#include "audio/Automation.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
#include "core/Markers.h"
#include "core/Project.h"
#include "core/Tempo.h"
#include "core/TempoChanges.h"

using namespace harmonia;
namespace changes = core::tempo_changes;
namespace clips = core::clips;

namespace
{
    double secondsAt (te::Edit& edit, double beat)
    {
        return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)).inSeconds();
    }
}

TEST_SUITE ("core::tempo changes")
{
    TEST_CASE ("a song has its tempo at beat 0; changes are added in time order, each from its beat on")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();
        project->setTempoBpm (120.0);
        project->clearUndoHistory();

        REQUIRE (changes::getAll (edit).size() == 1);
        CHECK (changes::getAll (edit)[0] == changes::Change { 0.0, 120.0, false });

        project->beginGesture ("Tempo change");
        CHECK (changes::add (edit, 16.0, 60.0) == 1);
        project->beginGesture ("Tempo change");
        CHECK (changes::add (edit, 8.0, 90.0) == 1);

        const auto all = changes::getAll (edit);
        REQUIRE (all.size() == 3);
        CHECK (all[1] == changes::Change { 8.0, 90.0, false });
        CHECK (all[2] == changes::Change { 16.0, 60.0, false });

        CHECK (changes::getIndexAt (edit, 0.0) == 0);
        CHECK (changes::getIndexAt (edit, 7.99) == 0);
        CHECK (changes::getIndexAt (edit, 8.0) == 1);
        CHECK (changes::getIndexAt (edit, 100.0) == 2);
        CHECK (changes::getBpmAt (edit, 4.0) == doctest::Approx (120.0));
        CHECK (changes::getBpmAt (edit, 12.0) == doctest::Approx (90.0));
        CHECK (changes::getBpmAt (edit, 20.0) == doctest::Approx (60.0));

        // Seconds: 8 beats at 120 (4 s), 8 at 90 (5.33 s), then 60.
        CHECK (secondsAt (edit, 8.0) == doctest::Approx (4.0));
        CHECK (secondsAt (edit, 16.0) == doctest::Approx (4.0 + 8.0 * 60.0 / 90.0));
        CHECK (secondsAt (edit, 18.0) == doctest::Approx (4.0 + 8.0 * 60.0 / 90.0 + 2.0));

        CHECK (changes::describe (edit, all[1]) == "bar 3: 90 BPM");

        // At a change that's there: its tempo is set, no second one.
        project->beginGesture ("Tempo change");
        CHECK (changes::add (edit, 8.1, 100.0) == 1);
        CHECK (changes::getAll (edit).size() == 3);
        CHECK (changes::getAll (edit)[1].bpm == doctest::Approx (100.0));

        // The limits of the engine.
        changes::setBpm (edit, 2, 1000.0);
        CHECK (changes::getAll (edit)[2].bpm == doctest::Approx (core::tempo::maxBpm));

        project->undo();
        CHECK (changes::getAll (edit)[1].bpm == doctest::Approx (90.0));
        project->undo();
        CHECK (changes::getAll (edit).size() == 2);
        project->undo();
        CHECK (changes::getAll (edit).size() == 1);
        project->redo();
        project->redo();
        CHECK (changes::getAll (edit).size() == 3);
    }

    TEST_CASE ("a change moves between its neighbours, glides or jumps, and is removed; the first stays")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();
        project->setTempoBpm (120.0);
        changes::add (edit, 8.0, 60.0);
        changes::add (edit, 16.0, 180.0);

        changes::move (edit, 1, 12.0);
        CHECK (changes::getAll (edit)[1].beat == doctest::Approx (12.0));
        changes::move (edit, 1, 40.0);      // not past the next
        CHECK (changes::getAll (edit)[1].beat == doctest::Approx (16.0 - changes::minGapBeats));
        changes::move (edit, 1, -5.0);      // nor the one before
        CHECK (changes::getAll (edit)[1].beat == doctest::Approx (changes::minGapBeats));
        changes::move (edit, 1, 8.0);
        changes::move (edit, 0, 4.0);       // the song's tempo is at its start
        CHECK (changes::getAll (edit)[0].beat == doctest::Approx (0.0));

        // A glide: from 120 at beat 0 to 60 at beat 8, half way about 90.
        changes::setGradual (edit, 1, true);
        CHECK (changes::getAll (edit)[1].gradual);
        CHECK_FALSE (changes::getAll (edit)[2].gradual);
        CHECK (changes::getBpmAt (edit, 0.0) == doctest::Approx (120.0).epsilon (0.02));
        CHECK (changes::getBpmAt (edit, 4.0) < 110.0);
        CHECK (changes::getBpmAt (edit, 4.0) > 70.0);
        CHECK (changes::getBpmAt (edit, 8.0) == doctest::Approx (60.0).epsilon (0.02));
        CHECK (changes::getBpmAt (edit, 12.0) == doctest::Approx (60.0).epsilon (0.02));    // then held
        CHECK (secondsAt (edit, 8.0) > 4.0);        // slower than 120 all the way
        CHECK (secondsAt (edit, 8.0) < 8.0);        // faster than 60 all the way
        CHECK (changes::describe (edit, changes::getAll (edit)[1]) == "bar 3: glide to 60 BPM");

        changes::setGradual (edit, 0, true);    // nothing before the first
        CHECK_FALSE (changes::getAll (edit)[0].gradual);

        // A change put into a glide is glided to, and on from.
        changes::add (edit, 4.0, 100.0);
        auto all = changes::getAll (edit);
        REQUIRE (all.size() == 4);
        CHECK (all[1].gradual);
        CHECK (all[2].gradual);

        // Removed, the glide goes to the next again.
        changes::remove (edit, 1);
        all = changes::getAll (edit);
        REQUIRE (all.size() == 3);
        CHECK (all[1] == changes::Change { 8.0, 60.0, true });

        changes::setGradual (edit, 1, false);
        CHECK (secondsAt (edit, 8.0) == doctest::Approx (4.0));

        changes::remove (edit, 1);
        CHECK (changes::getAll (edit).size() == 2);
        CHECK (changes::getBpmAt (edit, 10.0) == doctest::Approx (120.0));
        changes::remove (edit, 0);
        CHECK (changes::getAll (edit).size() == 2);
    }

    TEST_CASE ("clips, notes, automation and markers stay on their beats; changes are saved with the song")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();
        auto& track = *project->getTracks().getFirst();
        project->setTempoBpm (120.0);

        auto* early = project->addMidiClip (track, 4.0, 4.0, "Early");
        auto* late = project->addMidiClip (track, 16.0, 8.0, "Late");
        REQUIRE (early != nullptr);
        REQUIRE (late != nullptr);
        auto& volume = *track.getVolumePlugin()->volParam;
        audio::automation::addPoint (volume, 12.0, 0.5f);
        audio::automation::addPoint (volume, 20.0, 0.25f);
        const auto marker = core::markers::add (edit, 24.0, "Chorus");
        edit.dispatchPendingUpdatesSynchronously();

        const auto check = [&]
        {
            CHECK (clips::getStartBeat (*early) == doctest::Approx (4.0).epsilon (0.001));
            CHECK (clips::getEndBeat (*early) == doctest::Approx (8.0).epsilon (0.001));
            CHECK (clips::getStartBeat (*late) == doctest::Approx (16.0).epsilon (0.001));
            CHECK (clips::getEndBeat (*late) == doctest::Approx (24.0).epsilon (0.001));
            const auto points = audio::automation::getPoints (volume);
            REQUIRE (points.size() == 2);
            CHECK (points[0].beat == doctest::Approx (12.0).epsilon (0.001));
            CHECK (points[1].beat == doctest::Approx (20.0).epsilon (0.001));
            CHECK (core::markers::getAll (edit)[0].beat == doctest::Approx (24.0).epsilon (0.001));
        };

        changes::add (edit, 8.0, 60.0);
        check();
        changes::add (edit, 18.0, 200.0);
        check();
        changes::setGradual (edit, 1, true);
        check();
        changes::move (edit, 1, 10.0);
        check();
        changes::setBpm (edit, 1, 75.0);
        check();
        changes::remove (edit, 2);
        check();

        const auto before = changes::getAll (edit);
        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());

        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        CHECK (changes::getAll (loaded->getEdit()) == before);
        const auto loadedClips = clips::getClips (*loaded->getTracks().getFirst());
        REQUIRE (loadedClips.size() == 2);
        CHECK (clips::getStartBeat (*loadedClips[1]) == doctest::Approx (16.0).epsilon (0.001));
    }

    TEST_CASE ("the export plays the changes: a slower second half makes the song longer")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();
        auto& track = *project->getTracks().getFirst();
        project->setTempoBpm (120.0);
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        auto* clip = project->addMidiClip (track, 0.0, 16.0, "Notes");
        REQUIRE (clip != nullptr);
        clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
        clip->getSequence().addNote (64, te::BeatPosition::fromBeats (15.0), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
        edit.dispatchPendingUpdatesSynchronously();

        const auto lengthOf = [&] (const juce::String& name)
        {
            const auto file = temp.getFile (name);
            const auto result = audio::exportWav (edit, file);
            REQUIRE_MESSAGE (result.isEmpty(), result.toStdString());
            return te::AudioFile (edit.engine, file).getLength();
        };

        const auto straight = lengthOf ("Straight.wav");        // 16 beats at 120: 8 s and a tail
        changes::add (edit, 8.0, 60.0);                         // 8 beats at 120, 8 at 60: 12 s
        const auto slowed = lengthOf ("Slowed.wav");
        CHECK (slowed - straight == doctest::Approx (4.0).epsilon (0.05));
    }
}
