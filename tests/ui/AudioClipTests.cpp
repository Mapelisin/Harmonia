#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/arrangement/ArrangementGestures.h"

using namespace harmonia;
using namespace harmonia::ui::arrangement;
namespace clips = core::clips;
namespace tracks = core::tracks;

namespace
{
    constexpr int left = juce::ModifierKeys::leftButtonModifier;

    void dispatch (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** An instrument track with a 4-bar MIDI clip, a 2-second tone on disk (4 beats at 120 BPM)
        and the arrangement at 16 pixels per beat, snapping to bars. */
    struct AudioFixture
    {
        AudioFixture()
        {
            instrumentTrack = project->getTracks().getFirst();
            midiClip = project->addMidiClip (*instrumentTrack, 0.0, 16.0, "Clip");
            test::writeTone (tone, 2.0);

            view.width = 1000.0f;
            view.height = 400.0f;
            view.pixelsPerBeat = 16.0;
            model.setPixelsPerBeat (view.pixelsPerBeat);
            model.setSnap (core::snap::Mode::bar);
            project->clearUndoHistory();
        }

        te::AudioTrack* addAudioTrack()
        {
            auto* track = model.addAudioTrack();
            view.numTracks = model.getNumTracks();
            return track;
        }

        juce::Point<float> at (double beat, int trackIndex, float yInRow = 30.0f) const
        {
            return { view.beatToX (beat), view.trackToY (trackIndex) + yInRow };
        }

        /** A point on a clip's name strip: where a clip is taken to be moved. */
        juce::Point<float> onName (double beat, int trackIndex) const       { return at (beat, trackIndex, 7.0f); }

        void drag (juce::Point<float> from, juce::Point<float> to, int mods = 0)
        {
            const juce::ModifierKeys keys (mods | left);
            gestures.down ({ from, keys });

            for (int i = 1; i <= 10; ++i)
                gestures.drag ({ from + (to - from) * ((float) i / 10.0f), keys });

            gestures.up ({ to, keys });
        }

        test::TempFolder temp;   // first: deleted last
        juce::File tone = temp.getFile ("Tone.wav");
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* instrumentTrack = nullptr;
        te::MidiClip* midiClip = nullptr;

        ArrangementModel model { *project };
        ArrangementView view;
        ArrangementGestures gestures { model, view };
    };

    float exportRms (core::Project& project)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (project.getEdit(), file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        return buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    }
}

TEST_SUITE ("core: audio clips")
{
    TEST_CASE ("a sound file becomes an audio clip as long as the file")
    {
        AudioFixture f;
        auto* track = f.project->addTrack ("Audio");
        tracks::setKind (*track, tracks::Kind::audio);

        juce::String error;
        auto* clip = f.project->addAudioClip (*track, f.tone, 8.0, error);
        REQUIRE (clip != nullptr);
        CHECK (error.isEmpty());
        CHECK (clip->getName() == "Tone");
        CHECK (clips::isAudio (*clip));
        CHECK_FALSE (clips::isAudio (*f.midiClip));
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (8.0));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (12.0));
        CHECK (clips::getClips (*track) == juce::Array<te::Clip*> { clip });
        CHECK (clips::getMidiClips (*track).isEmpty());
        CHECK (clips::getSongEndBeat (f.project->getEdit()) == doctest::Approx (16.0));
    }

    TEST_CASE ("files that aren't readable audio are refused with a reason")
    {
        AudioFixture f;
        auto& track = *f.instrumentTrack;
        juce::String error;

        CHECK (f.project->addAudioClip (track, f.temp.getFile ("Nothing.wav"), 0.0, error) == nullptr);
        CHECK (error.contains ("not found"));

        const auto text = f.temp.getFile ("Notes.txt");
        text.replaceWithText ("not audio");
        CHECK_FALSE (f.project->canImport (text));
        CHECK (f.project->addAudioClip (track, text, 0.0, error) == nullptr);
        CHECK (error.contains ("Notes.txt"));

        const auto broken = f.temp.getFile ("Broken.wav");
        broken.replaceWithText ("RIFF but not really");
        CHECK (f.project->canImport (broken));
        CHECK (f.project->addAudioClip (track, broken, 0.0, error) == nullptr);
        CHECK (error.contains ("Broken.wav"));

        for (const auto* name : { "a.wav", "b.WAV", "c.aiff", "d.flac", "e.ogg" })
            CHECK_MESSAGE (f.project->canImport (juce::File::getCurrentWorkingDirectory().getChildFile (name)), name);
    }

    TEST_CASE ("an audio clip can't be stretched past the end of its file; trimming keeps the sound in place")
    {
        AudioFixture f;
        auto* track = f.addAudioTrack();
        juce::String error;
        auto* clip = f.project->addAudioClip (*track, f.tone, 4.0, error);
        REQUIRE (clip != nullptr);

        clips::setEnd (*clip, 100.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (8.0));

        clips::setStart (*clip, 5.0, 0.25);
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (5.0));
        CHECK (clips::getContentStartBeat (*clip) == doctest::Approx (4.0));
        CHECK (clip->getPosition().getOffset().inSeconds() == doctest::Approx (0.5));

        clips::setEnd (*clip, 6.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (6.0));
        clips::setEnd (*clip, 9.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (8.0));

        // Not before the start of the file.
        clips::setStart (*clip, 1.0, 0.25);
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (4.0));

        // Moved, it takes its file with it.
        clips::move (*clip, 12.0, *track);
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (12.0));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (16.0));
    }

    TEST_CASE ("a copy of an audio clip plays the same file and is independent")
    {
        AudioFixture f;
        auto* track = f.addAudioTrack();
        juce::String error;
        auto* clip = f.project->addAudioClip (*track, f.tone, 0.0, error);
        REQUIRE (clip != nullptr);
        clips::setEnd (*clip, 3.0, 0.25);

        auto* copy = clips::duplicate (*clip, 8.0, true);
        REQUIRE (copy != nullptr);
        CHECK (clips::isAudio (*copy));
        CHECK (clips::getStartBeat (*copy) == doctest::Approx (8.0));
        CHECK (clips::getEndBeat (*copy) == doctest::Approx (11.0));
        CHECK (dynamic_cast<te::AudioClipBase*> (copy)->getCurrentSourceFile() == f.tone);

        clips::setEnd (*copy, 10.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (3.0));

        clips::remove ({ clip });
        CHECK (clips::getClips (*track) == juce::Array<te::Clip*> { copy });
    }

    TEST_CASE ("the song's export plays the audio clips, as mute and solo say")
    {
        AudioFixture f;
        f.instrumentTrack->deleteRegion ({ te::TimePosition(), te::TimePosition::fromSeconds (100.0) }, nullptr);
        const auto quiet = test::writeTone (f.temp.getFile ("Quiet.wav"), 2.0, 0.02f);

        auto* loudTrack = f.addAudioTrack();
        auto* quietTrack = f.addAudioTrack();
        juce::String error;
        REQUIRE (f.project->addAudioClip (*loudTrack, f.tone, 0.0, error) != nullptr);
        REQUIRE (f.project->addAudioClip (*quietTrack, quiet, 0.0, error) != nullptr);
        CHECK (exportRms (*f.project) > 0.1f);

        loudTrack->setMute (true);
        CHECK (exportRms (*f.project) < 0.02f);
        CHECK (exportRms (*f.project) > 0.001f);
        CHECK (loudTrack->isMuted (false));   // still muted after the export

        loudTrack->setMute (false);
        quietTrack->setSolo (true);
        CHECK (exportRms (*f.project) < 0.02f);
        CHECK (quietTrack->isSolo (false));

        quietTrack->setSolo (false);
        loudTrack->setSolo (true);
        CHECK (exportRms (*f.project) > 0.1f);

        loudTrack->setSolo (false);

        for (auto* track : f.project->getTracks())
            track->setMute (true);

        juce::TemporaryFile file (".wav");
        CHECK (audio::exportWav (f.project->getEdit(), file.getFile()).contains ("muted"));
    }

    TEST_CASE ("saved and reopened, audio clips and track kinds come back; a missing file stays as a clip")
    {
        AudioFixture f;
        auto* track = f.addAudioTrack();
        juce::String error;
        REQUIRE (f.project->addAudioClip (*track, f.tone, 4.0, error) != nullptr);

        const auto projectFile = f.temp.getFile ("Song.harmonia");
        REQUIRE (f.project->save (projectFile).isEmpty());

        {
            auto loaded = core::Project::load (test::session(), projectFile, error);
            REQUIRE (loaded != nullptr);
            const auto loadedTracks = loaded->getTracks();
            REQUIRE (loadedTracks.size() == 2);
            CHECK_FALSE (tracks::isAudio (*loadedTracks[0]));
            CHECK (tracks::isAudio (*loadedTracks[1]));

            const auto loadedClips = clips::getClips (*loadedTracks[1]);
            REQUIRE (loadedClips.size() == 1);
            auto* audioClip = dynamic_cast<te::AudioClipBase*> (loadedClips[0]);
            REQUIRE (audioClip != nullptr);
            CHECK (audioClip->getCurrentSourceFile() == f.tone);
            CHECK (clips::getStartBeat (*audioClip) == doctest::Approx (4.0));
            CHECK (clips::getEndBeat (*audioClip) == doctest::Approx (8.0));
        }

        test::session().getEngine().getAudioFileManager().releaseAllFiles();
        REQUIRE (f.tone.deleteFile());

        auto loaded = core::Project::load (test::session(), projectFile, error);
        REQUIRE (loaded != nullptr);
        const auto loadedClips = clips::getClips (*loaded->getTracks()[1]);
        REQUIRE (loadedClips.size() == 1);
        CHECK_FALSE (dynamic_cast<te::AudioClipBase*> (loadedClips[0])->getCurrentSourceFile().existsAsFile());

        // The window shows it (as missing) without trouble.
        auto main = std::make_unique<ui::MainComponent> (test::session(), *loaded);
        main->setBounds (0, 0, 1280, 800);
        CHECK (main->createComponentSnapshot (main->getLocalBounds()).isValid());
        main.reset();
    }
}

TEST_SUITE ("arrangement: audio")
{
    TEST_CASE ("an audio track has no instrument and takes no MIDI clips; undo takes it away")
    {
        AudioFixture f;
        auto* track = f.addAudioTrack();
        REQUIRE (track != nullptr);
        CHECK (tracks::isAudio (*track));
        CHECK (track->getName() == "Audio 2");
        CHECK (audio::instruments::get (*track) == nullptr);
        CHECK (f.model.getCurrentTrack() == track);
        CHECK (f.model.getOpenClip() == nullptr);

        CHECK (f.model.createClip (*track, 0.0) == nullptr);
        f.gestures.down ({ f.at (40.0, 1), juce::ModifierKeys (left), 2 });
        f.gestures.up ({ f.at (40.0, 1), juce::ModifierKeys (left), 2 });
        CHECK (clips::getClips (*track).isEmpty());

        f.model.undo();
        CHECK (f.model.getNumTracks() == 1);
    }

    TEST_CASE ("dropped files land on an audio track, or on new audio tracks below an instrument track")
    {
        AudioFixture f;
        const auto second = test::writeTone (f.temp.getFile ("Second.wav"), 1.0);
        const auto third = test::writeTone (f.temp.getFile ("Third.wav"), 1.0, 0.5f, 2);
        auto* below = f.project->addTrack ("Below");
        f.project->clearUndoHistory();

        // On the instrument track: a new audio track right below it, the clip snapped to the bar.
        CHECK (f.model.importAudio ({ f.tone }, 0, 5.3).isEmpty());
        auto all = f.model.getTracks();
        REQUIRE (all.size() == 3);
        auto* audioTrack = all[1];
        CHECK (tracks::isAudio (*audioTrack));
        CHECK (audioTrack->getName() == "Tone");
        CHECK (all[2] == below);

        auto onTrack = clips::getClips (*audioTrack);
        REQUIRE (onTrack.size() == 1);
        CHECK (clips::getStartBeat (*onTrack[0]) == doctest::Approx (4.0));
        CHECK (f.model.getSelectedClips() == onTrack);
        CHECK (f.model.getCurrentTrack() == audioTrack);
        CHECK (f.model.getOpenClip() == nullptr);
        CHECK (f.model.getPlayMode() == PlayMode::song);

        // On the audio track: there.
        CHECK (f.model.importAudio ({ second }, 1, 16.0).isEmpty());
        CHECK (clips::getClips (*audioTrack).size() == 2);
        CHECK (f.model.getNumTracks() == 3);

        // Several files on empty space: a track each, at the bottom, in one undo step.
        CHECK (f.model.importAudio ({ second, third }, -1, 0.0).isEmpty());
        all = f.model.getTracks();
        REQUIRE (all.size() == 5);
        CHECK (all[3]->getName() == "Second");
        CHECK (all[4]->getName() == "Third");
        CHECK (f.model.getSelectedClips().size() == 2);

        f.model.undo();
        CHECK (f.model.getNumTracks() == 3);
        CHECK (clips::getClips (*audioTrack).size() == 2);

        // Unreadable files are named; the rest still come in.
        const auto text = f.temp.getFile ("Readme.txt");
        text.replaceWithText ("text");
        const auto problem = f.model.importAudio ({ text, second }, 1, 32.0);
        CHECK (problem.contains ("Readme.txt"));
        CHECK (clips::getClips (*audioTrack).size() == 3);
    }

    TEST_CASE ("clicking an audio clip makes its track current; clip mode plays that clip")
    {
        AudioFixture f;
        CHECK (f.model.importAudio ({ f.tone }, -1, 8.0).isEmpty());
        f.view.numTracks = f.model.getNumTracks();
        f.model.openClip (f.midiClip);
        f.model.setPlayMode (PlayMode::clip);
        CHECK (f.model.getPlayRange().end == doctest::Approx (16.0));

        f.gestures.down ({ f.at (10.0, 1), juce::ModifierKeys (left) });
        f.gestures.up ({ f.at (10.0, 1), juce::ModifierKeys (left) });
        CHECK (f.model.getCurrentTrack() == f.model.getTrack (1));
        CHECK (f.model.getOpenClip() == nullptr);
        CHECK (f.model.getPlayRange().start == doctest::Approx (8.0));
        CHECK (f.model.getPlayRange().end == doctest::Approx (12.0));

        f.gestures.down ({ f.at (2.0, 0), juce::ModifierKeys (left) });
        f.gestures.up ({ f.at (2.0, 0), juce::ModifierKeys (left) });
        CHECK (f.model.getOpenClip() == f.midiClip);
        CHECK (f.model.getPlayRange().end == doctest::Approx (16.0));
    }

    TEST_CASE ("audio clips move only between audio tracks, MIDI clips only between instrument tracks")
    {
        AudioFixture f;
        CHECK (f.model.importAudio ({ f.tone }, -1, 0.0).isEmpty());
        auto* first = f.model.getTrack (1);
        auto* second = f.addAudioTrack();
        auto* clip = clips::getClips (*first).getFirst();
        REQUIRE (clip != nullptr);
        dispatch();   // Tracktion sorts a track's clips a moment after they change, as the app would

        f.drag (f.onName (2.0, 1), f.at (2.0, 0));
        CHECK (clip->getClipTrack() == first);

        f.drag (f.onName (2.0, 1), f.at (18.0, 2));
        CHECK (clip->getClipTrack() == second);
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (16.0));
        dispatch();

        // Undo puts it back, and the arrangement shows it there.
        CHECK (f.project->canUndo());
        f.model.undo();
        dispatch();
        CHECK (f.project->canRedo());
        CHECK (clip->getClipTrack() == first);
        REQUIRE (f.model.getClips().size() == 2);
        CHECK (f.model.getClips()[1].trackIndex == 1);
        CHECK (f.model.getClips()[1].audio);
        f.model.redo();
        dispatch();
        CHECK (clip->getClipTrack() == second);
        CHECK (f.model.getClips()[1].trackIndex == 2);

        // A MIDI clip dragged onto an audio track stays home.
        f.drag (f.onName (8.0, 0), f.at (8.0, 1));
        CHECK (f.midiClip->getClipTrack() == f.instrumentTrack);

        // Shift-drag copies; Delete removes; the copy is a plain copy.
        f.drag (f.onName (17.0, 2), f.at (33.0, 2), juce::ModifierKeys::shiftModifier);
        const auto onSecond = clips::getClips (*second);
        REQUIRE (onSecond.size() == 2);
        CHECK (clips::getStartBeat (*onSecond[1]) == doctest::Approx (32.0));
        CHECK (clips::isAudio (*onSecond[1]));

        f.model.setSelection ({ onSecond[0] });
        f.model.duplicateSelected (true);
        CHECK (clips::getClips (*second).size() == 3);

        f.model.setSelection ({ onSecond[1] });
        f.model.deleteSelected();
        CHECK (clips::getClips (*second).size() == 2);
    }

    TEST_CASE ("files dragged over the arrangement show where they'll land and are added when dropped")
    {
        AudioFixture f;
        const auto text = f.temp.getFile ("Readme.txt");
        text.replaceWithText ("text");
        f.model.addAudioTrack();
        f.project->clearUndoHistory();

        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& arrangement = main->getArrangement();
        auto& clipArea = arrangement.getClipArea();
        auto& view = arrangement.getView();
        auto& model = main->getArrangementModel();
        view.pixelsPerBeat = 16.0;
        view.leftBeat = 0.0;
        model.setSnap (core::snap::Mode::bar);

        const auto point = [&] (double beat, int track)
        {
            return arrangement.getLocalPoint (&clipArea, juce::Point<float> (view.beatToX (beat), view.trackToY (track) + 20.0f)).toInt();
        };

        CHECK (arrangement.isInterestedInFileDrag ({ f.tone.getFullPathName() }));
        CHECK (arrangement.isInterestedInFileDrag ({ text.getFullPathName(), f.tone.getFullPathName() }));
        CHECK_FALSE (arrangement.isInterestedInFileDrag ({ text.getFullPathName() }));

        auto target = arrangement.getDropTarget (point (9.0, 1));
        CHECK (target.trackIndex == 1);
        CHECK (target.beat == doctest::Approx (8.0));
        CHECK_FALSE (target.newTrack);

        target = arrangement.getDropTarget (point (9.0, 0));
        CHECK (target.trackIndex == 0);
        CHECK (target.newTrack);

        target = arrangement.getDropTarget (point (9.0, 5));
        CHECK (target.trackIndex == -1);
        CHECK (target.newTrack);

        const juce::StringArray files { f.tone.getFullPathName() };
        arrangement.fileDragEnter (files, point (9.0, 1).x, point (9.0, 1).y);
        CHECK (clipArea.getDropMarker().has_value());
        CHECK (main->createComponentSnapshot (main->getLocalBounds()).isValid());
        arrangement.fileDragExit (files);
        CHECK_FALSE (clipArea.getDropMarker().has_value());

        arrangement.filesDropped (files, point (9.0, 1).x, point (9.0, 1).y);
        CHECK_FALSE (clipArea.getDropMarker().has_value());
        const auto onTrack = clips::getClips (*model.getTrack (1));
        REQUIRE (onTrack.size() == 1);
        CHECK (clips::getStartBeat (*onTrack[0]) == doctest::Approx (8.0));

        main.reset();
    }

    TEST_CASE ("the main window paints audio tracks with their waveforms")
    {
        AudioFixture f;
        const auto drums = test::writeTone (f.temp.getFile ("Drum loop.wav"), 8.0, 0.9f, 2, true);
        const auto vocal = test::writeTone (f.temp.getFile ("Vocal take.wav"), 6.0, 0.4f, 1);
        audio::instruments::useBuiltInSynth (*f.instrumentTrack);

        for (int i = 0; i < 16; ++i)
            f.midiClip->getSequence().addNote (48 + (i * 5) % 24, te::BeatPosition::fromBeats (i * 1.0),
                                               te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);

        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& model = main->getArrangementModel();

        CHECK (model.importAudio ({ drums }, -1, 0.0).isEmpty());
        CHECK (model.importAudio ({ drums }, 1, 16.0).isEmpty());
        CHECK (model.importAudio ({ vocal }, -1, 8.0).isEmpty());

        auto* vocalClip = clips::getClips (*model.getTrack (2)).getFirst();
        REQUIRE (vocalClip != nullptr);
        clips::setStart (*vocalClip, 9.0, 0.25);

        // A smooth fade in and out on the vocal, the second drum loop 6 dB down.
        auto* vocalAudio = dynamic_cast<te::AudioClipBase*> (vocalClip);
        REQUIRE (vocalAudio != nullptr);
        clips::setFadeShape (*vocalAudio, clips::FadeShape::sCurve);
        clips::setFadeInBeats (*vocalAudio, 2.0);
        clips::setFadeOutBeats (*vocalAudio, 3.0);
        auto* secondDrums = dynamic_cast<te::AudioClipBase*> (clips::getClips (*model.getTrack (1)).getLast());
        REQUIRE (secondDrums != nullptr);
        clips::setGainDb (*secondDrums, -6.0f);

        model.setSelection ({ vocalClip });
        model.focusClip (*vocalClip);
        main->getTransport().setPositionSeconds (5.0);

        // The waveforms are read in the background.
        auto& clipArea = main->getArrangement().getClipArea();
        juce::Image image;

        for (int i = 0; i < 100; ++i)
        {
            dispatch();
            image = main->createComponentSnapshot (main->getLocalBounds());

            if (clipArea.areWaveformsLoaded())
                break;
        }

        dispatch();
        CHECK (main->getArrangement().getHeaders().getNumHeaders() == 3);

        image = main->createComponentSnapshot (main->getLocalBounds());
        CHECK (clipArea.areWaveformsLoaded());
        CHECK (image.isValid());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-audio.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        main.reset();
    }
}
