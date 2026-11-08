#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tempo.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace tempo = core::tempo;
namespace clips = core::clips;

TEST_SUITE ("core::tempo")
{
    TEST_CASE ("tapping: the average of the last taps; a pause starts over")
    {
        tempo::TapTempo taps;
        CHECK_FALSE (taps.tap (10.0).has_value());          // one tap says nothing yet
        CHECK (taps.tap (10.5).value() == doctest::Approx (120.0));
        CHECK (taps.tap (11.0).value() == doctest::Approx (120.0));
        CHECK (taps.tap (11.52).value() == doctest::Approx (118.4).epsilon (0.001));   // a late tap: the average of three intervals

        // A pause: the next taps are a new tempo.
        CHECK_FALSE (taps.tap (20.0).has_value());
        CHECK (taps.tap (20.6).value() == doctest::Approx (100.0));
        CHECK (taps.getNumTaps() == 2);

        // Only the last four intervals count.
        tempo::TapTempo steady;

        for (const auto t : { 0.0, 1.0, 2.0, 2.5, 3.0, 3.5, 4.0 })
            steady.tap (t);

        CHECK (steady.tap (4.5).value() == doctest::Approx (120.0));

        // Within the engine's limits.
        tempo::TapTempo fast;
        fast.tap (0.0);
        CHECK (fast.tap (0.05).value() == doctest::Approx (tempo::maxBpm));
    }

    TEST_CASE ("tempo text and limits")
    {
        CHECK (tempo::toText (120.0) == "120 BPM");
        CHECK (tempo::toText (92.5) == "92.5 BPM");
        CHECK (tempo::toText (128.25) == "128.25 BPM");
        CHECK (tempo::clampBpm (5.0) == tempo::minBpm);
        CHECK (tempo::clampBpm (999.0) == tempo::maxBpm);
        CHECK (tempo::clampBpm (100.123) == doctest::Approx (100.12));
    }

    TEST_CASE ("a new tempo keeps clips and notes on their beats; audio clips keep their speed; undo")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& synth = *project->getTracks().getFirst();
        auto* midi = project->addMidiClip (synth, 4.0, 8.0, "Riff");
        REQUIRE (midi != nullptr);
        midi->getSequence().addNote (60, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

        auto* audioTrack = project->addTrack ("Audio");
        core::tracks::setKind (*audioTrack, core::tracks::Kind::audio);
        juce::String error;
        auto* audio = project->addAudioClip (*audioTrack, test::writeTone (temp.getFile ("Tone.wav"), 2.0), 8.0, error);
        REQUIRE (audio != nullptr);
        CHECK (clips::getEndBeat (*audio) == doctest::Approx (12.0));   // 2 s at 120 BPM
        project->clearUndoHistory();

        project->beginGesture ("Change tempo");
        project->setTempoBpm (60.0);
        CHECK (project->getTempoBpm() == doctest::Approx (60.0));

        CHECK (clips::getStartBeat (*midi) == doctest::Approx (4.0));
        CHECK (clips::getEndBeat (*midi) == doctest::Approx (12.0));
        CHECK (midi->getSequence().getNotes()[0]->getStartBeat().inBeats() == doctest::Approx (1.0));
        CHECK (midi->getPosition().getStart().inSeconds() == doctest::Approx (4.0));   // twice as late in time

        CHECK (clips::getStartBeat (*audio) == doctest::Approx (8.0));
        CHECK (audio->getPosition().getLength().inSeconds() == doctest::Approx (2.0));  // same sound
        CHECK (clips::getEndBeat (*audio) == doctest::Approx (10.0));

        project->undo();
        CHECK (project->getTempoBpm() == doctest::Approx (120.0));
        CHECK (midi->getPosition().getStart().inSeconds() == doctest::Approx (2.0));
        CHECK (clips::getEndBeat (*audio) == doctest::Approx (12.0));

        // Out of range is held to the limits.
        project->setTempoBpm (1000.0);
        CHECK (project->getTempoBpm() == doctest::Approx (tempo::maxBpm));
    }

    TEST_CASE ("the time signature moves the bar lines only; undo; saved")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        auto* clip = project->addMidiClip (track, 6.0, 4.0, "Clip");
        project->clearUndoHistory();
        CHECK (project->getBeatsPerBar() == 4);

        project->beginGesture ("Time signature");
        project->setBeatsPerBar (3);
        CHECK (project->getBeatsPerBar() == 3);
        CHECK (clips::getBeatsPerBar (project->getEdit()) == doctest::Approx (3.0));
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (6.0));
        CHECK (project->getEdit().tempoSequence.toBarsAndBeats (clip->getPosition().getStart()).bars == 2);   // bar 3 starts at beat 6

        const auto file = temp.getFile ("Waltz.harmonia");
        REQUIRE (project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE (loaded != nullptr);
        CHECK (loaded->getBeatsPerBar() == 3);

        project->undo();
        CHECK (project->getBeatsPerBar() == 4);
    }
}
