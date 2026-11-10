#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/DrumKit.h"
#include "audio/DrumSamples.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Slices.h"
#include "core/AudioClips.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/arrangement/ArrangementModel.h"

using namespace harmonia;
namespace clips = core::clips;
namespace slices = audio::slices;

namespace
{
    void dispatch (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** A song at 120 BPM with an audio track "Loops" and, on it at beat 4, a clip of eight hits
        0.6 s apart (a loop at 100 BPM) called "Break". */
    struct LoopSong
    {
        LoopSong()
        {
            project->setTempoBpm (120.0);
            track = project->addTrack ("Loops");
            core::tracks::setKind (*track, core::tracks::Kind::audio);

            for (int i = 0; i < 8; ++i)
                hits.push_back (0.6 * i);

            file = test::writeClicks (temp.getFile ("Break.wav"), hits, 0.6);
            juce::String error;
            clip = project->addAudioClip (*track, file, 4.0, error);
            REQUIRE_MESSAGE (clip != nullptr, error.toStdString());
            dispatch();
            project->clearUndoHistory();
        }

        /** Where the hits of the song as exported are, in seconds. */
        std::vector<double> exportedClicks (float threshold = 0.2f)
        {
            const auto out = temp.getFile ("Song " + juce::String (++renders) + ".wav");
            REQUIRE (audio::exportWav (project->getEdit(), out).isEmpty());
            return test::findClicks (out, threshold);
        }

        void deletePieces (const slices::Sliced& sliced)
        {
            test::session().getEngine().getAudioFileManager().releaseAllFiles();

            for (const auto& slice : sliced.slices)
                slice.file.deleteFile();
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* track = nullptr;
        te::WaveAudioClip* clip = nullptr;
        juce::File file;
        std::vector<double> hits;
        int renders = 0;
    };
}

TEST_SUITE ("audio: a loop cut onto the keys of a drum machine")
{
    TEST_CASE ("a loop is cut at its hits: a file a hit, a key a file, each where it was in the song")
    {
        LoopSong song;
        juce::String problem;
        const auto sliced = slices::prepare (*song.clip, problem);
        REQUIRE_MESSAGE (sliced.has_value(), problem.toStdString());
        CHECK (sliced->name == "Break");
        CHECK (sliced->startBeat == doctest::Approx (4.0));
        REQUIRE (sliced->slices.size() == 8);

        // At its own speed the loop's hits are 1.2 beats of the song apart.
        for (size_t i = 0; i < 8; ++i)
        {
            CAPTURE (i);
            const auto& slice = sliced->slices[i];
            CHECK (slice.key == slices::firstKey + (int) i);
            CHECK (std::abs (slice.startBeat - (4.0 + 1.2 * (double) i)) < 0.01);
            CHECK (std::abs (slice.lengthBeats - 1.2) < 0.01);
            REQUIRE (slice.file.existsAsFile());
            CHECK (slice.file.getFileName() == "Break slice " + juce::String ((int) i + 1).paddedLeft ('0', 2) + ".wav");

            // The piece begins with its hit and is as long as the gap to the next.
            const auto clicks = test::findClicks (slice.file);
            REQUIRE (clicks.size() == 1);
            CHECK (clicks[0] < 0.006);
            const te::AudioFile piece (test::session().getEngine(), slice.file);
            CHECK (piece.getLength() == doctest::Approx (0.6).epsilon (0.01));
        }

        // Nothing in the song has changed yet.
        CHECK_FALSE (song.project->canUndo());
        CHECK (clips::getClips (*song.track).size() == 1);
        song.deletePieces (*sliced);
    }

    TEST_CASE ("the pieces on a drum track, with their clip of notes, play the loop as it went")
    {
        LoopSong song;
        const auto before = song.exportedClicks();
        REQUIRE (before.size() == 8);

        juce::String problem;
        const auto sliced = slices::prepare (*song.clip, problem);
        REQUIRE_MESSAGE (sliced.has_value(), problem.toStdString());

        const auto endBeat = clips::getEndBeat (*song.clip);
        song.project->beginGesture ("Slice");
        auto* drumTrack = song.project->addTrack ("Slices");
        REQUIRE (audio::instruments::makeDrumTrack (*drumTrack).isEmpty());
        auto* notes = slices::apply (*song.project, *drumTrack, *sliced);
        REQUIRE (notes != nullptr);
        clips::remove ({ song.clip });
        dispatch();

        // Keys C1 to G1 have the pieces; the clip has a note for each, where the hit was.
        auto* drums = dynamic_cast<audio::drum_kit::Plugin*> (audio::instruments::get (*drumTrack));
        REQUIRE (drums != nullptr);
        CHECK (audio::drum_kit::getSampledKeys (*drums) == std::vector<int> { 36, 37, 38, 39, 40, 41, 42, 43 });
        CHECK (audio::drum_kit::getSample (*drums, 38)->file == sliced->slices[2].file);
        CHECK (clips::getStartBeat (*notes) == doctest::Approx (4.0));
        CHECK (clips::getEndBeat (*notes) == doctest::Approx (endBeat).epsilon (0.001));
        CHECK (notes->getName() == "Break");

        auto played = clips::getPlayedNotes (*notes);
        std::sort (played.begin(), played.end(), [] (const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        REQUIRE (played.size() == 8);

        for (size_t i = 0; i < 8; ++i)
        {
            CAPTURE (i);
            CHECK (played[i].note.pitch == 36 + (int) i);
            CHECK (std::abs (played[i].startBeat - (4.0 + 1.2 * (double) i)) < 0.01);
        }

        // The song sounds as it did: the hits where they were.
        drums->updateSamplesNow();
        const auto after = song.exportedClicks (0.1f);
        REQUIRE (after.size() == 8);

        for (size_t i = 0; i < 8; ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (after[i] - before[i]) < 0.004);
        }

        // One Ctrl+Z: the loop as an audio clip again, no drum track.
        song.project->undo();
        dispatch();
        CHECK (song.project->getTracks().size() == 2);       // the song's first track, and the loops
        CHECK (clips::getClips (*song.track).size() == 1);
        song.deletePieces (*sliced);
    }

    TEST_CASE ("a loop stretched to the song's tempo: the pieces start on the song's beats; its gain and pitch go to the keys")
    {
        LoopSong song;
        song.project->beginGesture ("Follow the tempo");
        core::audio_tempo::setFollowsTempo (*song.clip, 100.0);
        clips::setGainDb (*song.clip, -6.0f);
        core::audio_tempo::setPitch (*song.clip, 2.0f);
        dispatch();

        juce::String problem;
        const auto sliced = slices::prepare (*song.clip, problem);
        REQUIRE_MESSAGE (sliced.has_value(), problem.toStdString());
        REQUIRE (sliced->slices.size() == 8);
        CHECK (sliced->gainDb == doctest::Approx (-6.0f));
        CHECK (sliced->tune == doctest::Approx (2.0f));

        for (size_t i = 0; i < 8; ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (sliced->slices[i].startBeat - (4.0 + (double) i)) < 0.01);
            CHECK (std::abs (sliced->slices[i].lengthBeats - 1.0) < 0.01);
        }

        auto* drumTrack = song.project->addTrack ("Slices");
        REQUIRE (audio::instruments::makeDrumTrack (*drumTrack).isEmpty());
        REQUIRE (slices::apply (*song.project, *drumTrack, *sliced) != nullptr);
        const auto sample = audio::drum_kit::getSample (*audio::instruments::get (*drumTrack), 36);
        REQUIRE (sample.has_value());
        CHECK (sample->gainDb == doctest::Approx (-6.0f));
        CHECK (sample->tune == doctest::Approx (2.0f));
        song.deletePieces (*sliced);
    }

    TEST_CASE ("what can't be cut says why: a sound with no hits, a clip that repeats; many hits keep the strongest")
    {
        LoopSong song;
        juce::String error, problem;

        // A held tone: nothing to cut it at.
        auto* tone = song.project->addAudioClip (*song.track, test::writeTone (song.temp.getFile ("Pad.wav"), 3.0, 0.3f), 40.0, error);
        REQUIRE (tone != nullptr);
        CHECK_FALSE (slices::prepare (*tone, problem).has_value());
        CHECK (problem.startsWith ("No hits to cut it at"));

        // A clip that repeats.
        core::audio_clips::setLooping (*song.clip, true);
        problem.clear();
        CHECK_FALSE (slices::prepare (*song.clip, problem).has_value());
        CHECK (problem.startsWith ("A clip that repeats can't be sliced"));
        core::audio_clips::setLooping (*song.clip, false);

        // Ninety hits, every third one loud: the keys go to the loud ones first.
        std::vector<double> hits;
        std::vector<float> levels;

        for (int i = 0; i < 90; ++i)
        {
            hits.push_back (0.25 * i);
            levels.push_back (i % 3 == 0 ? 0.9f : 0.25f);
        }

        auto* many = song.project->addAudioClip (*song.track, test::writeClicks (song.temp.getFile ("Many.wav"), hits, 0.25, levels), 80.0, error);
        REQUIRE (many != nullptr);
        problem.clear();
        const auto sliced = slices::prepare (*many, problem);
        REQUIRE_MESSAGE (sliced.has_value(), problem.toStdString());
        CHECK (sliced->slices.size() == (size_t) slices::mostSlices);

        for (size_t i = 1; i < sliced->slices.size(); ++i)
            CHECK (sliced->slices[i].startBeat > sliced->slices[i - 1].startBeat);

        // Every loud hit begins a piece.
        for (int i = 0; i < 90; i += 3)
        {
            const auto beat = 80.0 + 0.5 * i;
            CAPTURE (i);
            CHECK (std::any_of (sliced->slices.begin(), sliced->slices.end(), [beat] (const slices::Slice& s) { return std::abs (s.startBeat - beat) < 0.01; }));
        }

        song.deletePieces (*sliced);
    }

    TEST_CASE ("the arrangement's command: a drum track under the clip's track takes its place; one undo step")
    {
        LoopSong song;
        ui::arrangement::ArrangementModel model (*song.project);
        dispatch();
        const auto tracksBefore = song.project->getTracks().size();

        const auto made = model.sliceToDrums (*song.clip);
        dispatch();
        REQUIRE_MESSAGE (made.clip != nullptr, made.problem.toStdString());
        CHECK (made.problem.isEmpty());
        CHECK (made.pieces == 8);
        REQUIRE (made.track != nullptr);

        // Right under the loops, named after the clip, playing the drum machine; the audio clip is gone.
        const auto tracks = song.project->getTracks();
        REQUIRE (tracks.size() == tracksBefore + 1);
        CHECK (tracks.indexOf (made.track) == tracks.indexOf (song.track) + 1);
        CHECK (made.track->getName() == "Break slices");
        CHECK (audio::instruments::playsDrumKit (*made.track));
        CHECK (clips::getClips (*song.track).isEmpty());
        CHECK (clips::getClips (*made.track).size() == 1);
        CHECK (model.getCurrentTrack() == made.track);
        CHECK (model.getSelectedClips().contains (made.clip));
        CHECK (song.project->getUndoDescription() == "Slice to a drum track");

        // The files the keys play (to tidy up after the test).
        std::vector<juce::File> pieces;

        for (const auto key : audio::drum_kit::getSampledKeys (*audio::instruments::get (*made.track)))
            pieces.push_back (audio::drum_kit::getSample (*audio::instruments::get (*made.track), key)->file);

        CHECK (pieces.size() == 8);

        // One Ctrl+Z: the audio clip back, the drum track gone.
        song.project->undo();
        dispatch();
        CHECK (song.project->getTracks().size() == tracksBefore);
        CHECK (clips::getClips (*song.track).size() == 1);
        CHECK_FALSE (song.project->canUndo());

        // A sound with no hits: said, nothing made, no undo step.
        juce::String error;
        auto* tone = song.project->addAudioClip (*song.track, test::writeTone (song.temp.getFile ("Pad.wav"), 3.0, 0.3f), 40.0, error);
        REQUIRE (tone != nullptr);
        dispatch();
        song.project->clearUndoHistory();
        const auto none = model.sliceToDrums (*tone);
        CHECK (none.clip == nullptr);
        CHECK (none.problem.startsWith ("No hits to cut it at"));
        CHECK (song.project->getTracks().size() == tracksBefore);
        CHECK_FALSE (song.project->canUndo());

        test::session().getEngine().getAudioFileManager().releaseAllFiles();

        for (const auto& piece : pieces)
            piece.deleteFile();
    }
}
