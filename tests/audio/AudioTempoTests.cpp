#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Consolidate.h"
#include "audio/Export.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/TempoChanges.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace audio_tempo = core::audio_tempo;

namespace
{
    constexpr double rate = 44100.0;

    /** Writes `buffer` (mono) as a WAV, with optional metadata (ACID loop data). */
    juce::File writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer,
                         const std::unordered_map<juce::String, juce::String>& metadata = {})
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (1)
                                                       .withBitsPerSample (24)
                                                       .withMetadataValues (metadata));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    /** A loop of `beats` beats at `bpm`: a short click on each beat, silence between. */
    juce::File writeClickLoop (const juce::File& file, double bpm, int beats)
    {
        const auto beatSamples = rate * 60.0 / bpm;
        juce::AudioBuffer<float> buffer (1, (int) std::round (beatSamples * beats));
        buffer.clear();

        for (int beat = 0; beat < beats; ++beat)
        {
            const auto start = (int) std::round (beat * beatSamples);

            for (int i = 0; i < (int) (0.01 * rate) && start + i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, start + i, 0.9f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate)
                                                     * (float) std::exp (-i / (0.003 * rate)));
        }

        return writeWav (file, buffer);
    }

    /** Clicks on `beats` beats at `bpm`, with silence before the first and after the last beat. */
    juce::File writeClicks (const juce::File& file, double bpm, int beats, double leadSeconds, double tailSeconds)
    {
        const auto beatSamples = rate * 60.0 / bpm;
        juce::AudioBuffer<float> buffer (1, (int) std::round (leadSeconds * rate + beatSamples * beats + tailSeconds * rate));
        buffer.clear();

        for (int beat = 0; beat < beats; ++beat)
        {
            const auto start = (int) std::round (leadSeconds * rate + beat * beatSamples);

            for (int i = 0; i < (int) (0.01 * rate) && start + i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, start + i, 0.9f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate)
                                                     * (float) std::exp (-i / (0.003 * rate)));
        }

        return writeWav (file, buffer);
    }

    /** A click at each of `beats` (seconds), then `tailSeconds` of silence. */
    juce::File writeClicksAt (const juce::File& file, const std::vector<double>& beats, double tailSeconds)
    {
        juce::AudioBuffer<float> buffer (1, (int) std::round ((beats.back() + tailSeconds) * rate));
        buffer.clear();

        for (const auto beat : beats)
        {
            const auto start = (int) std::round (beat * rate);

            for (int i = 0; i < (int) (0.01 * rate) && start + i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, start + i, 0.9f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / rate)
                                                     * (float) std::exp (-i / (0.003 * rate)));
        }

        return writeWav (file, buffer);
    }

    /** Where somebody speeding up from `fromBpm` to `toBpm` has `count` beats, the first `lead`
        seconds in. */
    std::vector<double> speedingUp (int count, double lead, double fromBpm, double toBpm)
    {
        std::vector<double> beats;
        auto at = lead;

        for (int i = 0; i < count; ++i)
        {
            beats.push_back (at);
            at += 60.0 / (fromBpm + (toBpm - fromBpm) * i / (count - 1.0));
        }

        return beats;
    }

    /** Lets the engine write the reversed copy of a clip's file (in the background, from a file
        the test deletes when it ends); false if it takes too long. */
    bool waitWhileReversing (const te::AudioClipBase& clip)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + 15000.0;

        while (core::clips::isReversing (clip))
        {
            if (juce::Time::getMillisecondCounterHiRes() > end)
                return false;

            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        return true;
    }

    juce::File writeSine (const juce::File& file, double seconds, double hz)
    {
        juce::AudioBuffer<float> buffer (1, (int) (seconds * rate));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));

        return writeWav (file, buffer);
    }

    juce::AudioBuffer<float> readWav (const juce::File& file, double& sampleRate)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        sampleRate = reader->sampleRate;
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer;
    }

    /** Where the clicks of a rendered song start, in seconds: loud after a tenth of a second without. */
    std::vector<double> findClicks (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        std::vector<double> clicks;
        auto lastLoud = -(int) sampleRate;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (std::abs (buffer.getSample (0, i)) <= 0.2f)
                continue;

            if (i - lastLoud > (int) (0.1 * sampleRate))
                clicks.push_back (i / sampleRate);

            lastLoud = i;
        }

        return clicks;
    }

    /** `count` clicks, one every `seconds` from the start, each within 3 ms (what stretching smears
        of the start of a click). */
    void checkClicks (const std::vector<double>& clicks, size_t count, double seconds)
    {
        REQUIRE (clicks.size() == count);

        for (size_t i = 0; i < count; ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (clicks[i] - seconds * (double) i) < 0.003);
        }
    }

    /** Zero crossings per second over the middle of the buffer: twice the pitch of a sine. */
    double crossingsPerSecond (const juce::AudioBuffer<float>& buffer, double sampleRate, double fromSeconds, double toSeconds)
    {
        const auto from = (int) (fromSeconds * sampleRate), to = (int) (toSeconds * sampleRate);
        int crossings = 0;

        for (int i = from + 1; i < to; ++i)
            if ((buffer.getSample (0, i - 1) < 0.0f) != (buffer.getSample (0, i) < 0.0f))
                ++crossings;

        return crossings / (toSeconds - fromSeconds);
    }

    struct AudioSong
    {
        AudioSong()
        {
            track = project->addTrack ("Loops");
            core::tracks::setKind (*track, core::tracks::Kind::audio);
            project->clearUndoHistory();
        }

        te::WaveAudioClip* add (const juce::File& file, double beat = 0.0)
        {
            juce::String error;
            auto* clip = project->addAudioClip (*track, file, beat, error);
            REQUIRE_MESSAGE (clip != nullptr, error.toStdString());
            return clip;
        }

        juce::AudioBuffer<float> render (double& sampleRate, std::optional<te::TimeRange> part = std::nullopt)
        {
            const auto out = temp.getFile ("Song " + juce::String (++renders) + ".wav");
            REQUIRE (audio::exportWav (project->getEdit(), out, part).isEmpty());
            return readWav (out, sampleRate);
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* track = nullptr;
        int renders = 0;
    };
}

TEST_SUITE ("audio: clips follow the song tempo")
{
    TEST_CASE ("a loop's tempo is found from its data, its name, or its length when called a loop; never for a hit or a phrase")
    {
        test::TempFolder temp;
        auto& engine = test::session().getEngine();

        const auto named = writeClickLoop (temp.getFile ("Drums 95 BPM.wav"), 95.0, 16);
        CHECK (audio_tempo::findLoopBpm (engine, named, 120.0).value_or (0.0) == doctest::Approx (95.0).epsilon (0.002));

        const auto underscores = writeClickLoop (temp.getFile ("hat_loop_140_Am.wav"), 140.0, 8);
        CHECK (audio_tempo::findLoopBpm (engine, underscores, 120.0).value_or (0.0) == doctest::Approx (140.0).epsilon (0.002));

        // ACID data says 8 beats in 4 seconds: 120 BPM, whatever the name.
        juce::AudioBuffer<float> riff (1, (int) (4.0 * rate));
        riff.clear();
        const auto acid = writeWav (temp.getFile ("Riff.wav"), riff, { { juce::WavAudioFormat::acidBeats, "8" },
                                                                       { juce::WavAudioFormat::acidNumerator, "4" },
                                                                       { juce::WavAudioFormat::acidDenominator, "4" },
                                                                       { juce::WavAudioFormat::acidOneShot, "0" } });
        CHECK (audio_tempo::findLoopBpm (engine, acid, 90.0).value_or (0.0) == doctest::Approx (120.0).epsilon (0.002));

        // Called a loop, no tempo: the whole bars nearest the song's tempo.
        const auto guitar = writeSine (temp.getFile ("Guitar loop.wav"), 2.0, 220.0);
        CHECK (audio_tempo::findLoopBpm (engine, guitar, 120.0).value_or (0.0) == doctest::Approx (120.0));
        CHECK (audio_tempo::guessFromLength (8.0, 100.0).value_or (0.0) == doctest::Approx (120.0));   // 16 beats
        CHECK (audio_tempo::guessFromLength (6.0, 90.0).value_or (0.0) == doctest::Approx (80.0));     // 8 beats, not 160
        CHECK_FALSE (audio_tempo::guessFromLength (0.5, 120.0).has_value());

        // A drum hit, a sung phrase: their own speed.
        CHECK_FALSE (audio_tempo::findLoopBpm (engine, writeSine (temp.getFile ("Kick.wav"), 0.3, 60.0), 120.0).has_value());
        CHECK_FALSE (audio_tempo::findLoopBpm (engine, writeSine (temp.getFile ("Vocal phrase.wav"), 3.7, 300.0), 120.0).has_value());
    }

    TEST_CASE ("a loop at 100 BPM plays on the beats of a song at 120, and of the song at any new tempo; off, its own speed again")
    {
        // 32 beats: long enough for the beats to drift off the song's if stretching lost time.
        AudioSong song;
        auto* clip = song.add (writeClickLoop (song.temp.getFile ("Clicks 100 BPM.wav"), 100.0, 32));
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (19.2));
        CHECK_FALSE (audio_tempo::followsTempo (*clip));

        song.project->beginGesture ("Follow the tempo");
        audio_tempo::setFollowsTempo (*clip, 100.0);
        CHECK (audio_tempo::followsTempo (*clip));
        CHECK (audio_tempo::getLoopBpm (*clip).value_or (0.0) == doctest::Approx (100.0));
        CHECK (core::clips::getEndBeat (*clip) == doctest::Approx (32.0));               // 32 beats of the loop = 32 of the song
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (16.0));

        double sampleRate = 0.0;
        auto rendered = song.render (sampleRate);
        checkClicks (findClicks (rendered, sampleRate), 32, 0.5);

        // Played from the middle (beat 6): still on the beats.
        rendered = song.render (sampleRate, te::TimeRange (te::TimePosition::fromSeconds (3.0), te::TimePosition::fromSeconds (16.0)));
        checkClicks (findClicks (rendered, sampleRate), 26, 0.5);

        // A slower song: the loop slows down with it, still 32 beats.
        song.project->beginGesture ("Change tempo");
        song.project->setTempoBpm (80.0);
        CHECK (core::clips::getEndBeat (*clip) == doctest::Approx (32.0));
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (24.0));

        rendered = song.render (sampleRate);
        checkClicks (findClicks (rendered, sampleRate), 32, 0.75);

        // Off: the file's own speed, 19.2 s.
        song.project->beginGesture ("Own speed");
        audio_tempo::setFollowsTempo (*clip, std::nullopt);
        CHECK_FALSE (audio_tempo::followsTempo (*clip));
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (19.2));

        // Undo all the way back.
        song.project->getEdit().dispatchPendingUpdatesSynchronously();
        song.project->undo();
        CHECK (audio_tempo::followsTempo (*clip));
        song.project->undo();
        song.project->undo();
        CHECK_FALSE (audio_tempo::followsTempo (*clip));
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (19.2));
    }

    TEST_CASE ("the tempo of a sound is measured from the sound: 72.3 BPM with no name and no data, silence before it")
    {
        test::TempFolder temp;
        auto& engine = test::session().getEngine();

        const auto mystery = writeClicks (temp.getFile ("Mystery.wav"), 72.3, 16, 0.25, 0.37);
        CHECK_FALSE (audio_tempo::findLoopBpm (engine, mystery, 150.0).has_value());      // nothing says its tempo

        // In a song at 75 it is what it is; in one at 150, the same hits twice as fast.
        auto measured = audio_tempo::measure (engine, mystery, 75.0);
        REQUIRE (measured.has_value());
        CHECK (measured->bpm == doctest::Approx (72.3).epsilon (0.0005));
        CHECK (std::abs (measured->firstBeatSeconds - 0.25) < 0.003);
        CHECK_FALSE (measured->wholeLoop);

        measured = audio_tempo::measure (engine, mystery, 150.0);
        REQUIRE (measured.has_value());
        CHECK (measured->bpm == doctest::Approx (144.6).epsilon (0.0005));
        CHECK (std::abs (measured->firstBeatSeconds - 0.25) < 0.003);

        // Cut to length it is a loop: exact, from its first sample.
        measured = audio_tempo::measure (engine, writeClickLoop (temp.getFile ("Cut.wav"), 72.3, 16), 75.0);
        REQUIRE (measured.has_value());
        CHECK (measured->wholeLoop);
        CHECK (measured->bpm == doctest::Approx (72.3).epsilon (0.0001));
        CHECK (measured->firstBeatSeconds == 0.0);

        // A stereo file as well.
        {
            juce::AudioBuffer<float> stereo (2, (int) (8.0 * rate));
            stereo.clear();

            for (int beat = 0; beat < 16; ++beat)
                for (int i = 0; i < 400; ++i)
                    stereo.setSample (beat % 2, (int) (beat * 0.5 * rate) + i, 0.8f * (float) std::sin (i * 0.14) * (float) std::exp (-i / 130.0));

            const auto file = temp.getFile ("Wide.wav");
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (rate)
                                                                              .withNumChannels (2).withBitsPerSample (16));
            REQUIRE (writer != nullptr);
            writer->writeFromAudioSampleBuffer (stereo, 0, stereo.getNumSamples());
            writer.reset();

            measured = audio_tempo::measure (engine, file, 120.0);
            REQUIRE (measured.has_value());
            CHECK (measured->bpm == doctest::Approx (120.0).epsilon (0.0002));
        }

        // Nothing to measure: a held tone, a file that isn't there.
        CHECK_FALSE (audio_tempo::measure (engine, writeSine (temp.getFile ("Pad.wav"), 4.0, 220.0), 120.0).has_value());
        CHECK_FALSE (audio_tempo::measure (engine, temp.getFile ("No such file.wav"), 120.0).has_value());
    }

    TEST_CASE ("measured and following, the hits of a sound at 72.3 BPM are on the grid of a song at 150; the sound slides by sixteenths")
    {
        AudioSong song;
        song.project->setTempoBpm (150.0);
        const auto file = writeClicks (song.temp.getFile ("Mystery.wav"), 72.3, 16, 0.25, 0.37);
        auto* clip = song.add (file, 4.0);
        song.project->getEdit().dispatchPendingUpdatesSynchronously();

        const auto measured = audio_tempo::measure (test::session().getEngine(), file, 150.0);
        REQUIRE (measured.has_value());

        song.project->beginGesture ("Follow the tempo");
        audio_tempo::setFollowsTempo (*clip, measured->bpm);
        const auto hidden = audio_tempo::startOnBeat (*clip, *measured);
        CHECK (std::abs (hidden - 0.25) < 0.003);
        CHECK (core::clips::getStartBeat (*clip) == doctest::Approx (4.0));

        // Each beat of the sound is two beats of the song (0.8 s), the first where the clip starts
        // (beat 4 of the song: 1.6 s). 16 beats and the tail: 32 beats and a little.
        CHECK (core::clips::getEndBeat (*clip) - core::clips::getStartBeat (*clip) == doctest::Approx (32.0 + 0.37 * 144.6 / 60.0).epsilon (0.002));

        // Where the clicks of the song as exported are.
        const auto exportedClicks = [&song]
        {
            double sampleRate = 0.0;
            const auto rendered = song.render (sampleRate);
            return findClicks (rendered, sampleRate);
        };

        auto clicks = exportedClicks();
        REQUIRE (clicks.size() == 16);

        for (size_t i = 0; i < clicks.size(); ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (clicks[i] - (1.6 + 0.8 * (double) i)) < 0.004);
        }

        // Starting on a beat again changes nothing.
        CHECK (audio_tempo::startOnBeat (*clip, *measured) == 0.0);

        // A sixteenth later, the clip staying where it is: every hit 0.1 s later.
        CHECK (audio_tempo::slide (*clip, 0.25));
        CHECK (core::clips::getStartBeat (*clip) == doctest::Approx (4.0));
        clicks = exportedClicks();
        REQUIRE (clicks.size() == 16);
        CHECK (std::abs (clicks[0] - 1.7) < 0.004);
        CHECK (std::abs (clicks[15] - (1.7 + 0.8 * 15.0)) < 0.004);

        // Two sixteenths earlier: the first hit is now before the clip, the second 0.7 s in.
        CHECK (audio_tempo::slide (*clip, -0.5));
        clicks = exportedClicks();
        REQUIRE (clicks.size() == 15);
        CHECK (std::abs (clicks[0] - (1.6 + 0.8 - 0.1)) < 0.004);

        // Later only as far as the file has sound before: then it stops.
        CHECK (audio_tempo::slide (*clip, 100.0));
        CHECK_FALSE (audio_tempo::slide (*clip, 1.0));
        clicks = exportedClicks();
        REQUIRE (clicks.size() == 16);
        CHECK (std::abs (clicks[0] - (1.6 + 0.25 * 144.6 / 150.0)) < 0.004);      // the silence before it, at the song's speed

        // One Ctrl+Z: its own speed, its whole file, as dropped.
        song.project->undo();
        CHECK_FALSE (audio_tempo::followsTempo (*clip));
        CHECK (clip->getPosition().getOffset().inSeconds() == doctest::Approx (0.0));
        CHECK_FALSE (audio_tempo::slide (*clip, 0.25));
    }

    TEST_CASE ("a sound that speeds up follows beat by beat: every hit on a beat of the song, whatever the song's tempo")
    {
        AudioSong song;
        song.project->setTempoBpm (100.0);

        // 32 hits, from 92 to 104 BPM, a quarter of a second of silence before them.
        const auto beats = speedingUp (32, 0.25, 92.0, 104.0);
        const auto file = writeClicksAt (song.temp.getFile ("Played.wav"), beats, 0.5);
        auto* clip = song.add (file, 4.0);
        song.project->getEdit().dispatchPendingUpdatesSynchronously();

        const auto measured = audio_tempo::measure (test::session().getEngine(), file, 100.0);
        REQUIRE (measured.has_value());
        REQUIRE (measured->drifts());
        REQUIRE (measured->beatSeconds.size() == 32);

        song.project->beginGesture ("Follow the tempo");
        audio_tempo::setFollowsTempo (*clip, *measured);
        const auto hidden = audio_tempo::startOnBeat (*clip, *measured);
        CHECK (std::abs (hidden - 0.25) < 0.003);
        CHECK (audio_tempo::followsTempo (*clip));
        CHECK (audio_tempo::followsBeatByBeat (*clip));
        CHECK (core::clips::getStartBeat (*clip) == doctest::Approx (4.0));

        // 31 beats from the first hit to the last, and the tail after it at the last tempo heard.
        const auto tailBeats = 0.5 / (beats[31] - beats[30]);
        CHECK (core::clips::getEndBeat (*clip) - core::clips::getStartBeat (*clip) == doctest::Approx (31.0 + tailBeats).epsilon (0.003));

        // Where the hits are in the song as exported: `first` seconds in, then every `apart`.
        const auto checkExport = [&song] (core::Project& project, double first, double apart)
        {
            const auto out = song.temp.getFile ("Played " + juce::String (++song.renders) + ".wav");
            REQUIRE (audio::exportWav (project.getEdit(), out).isEmpty());
            double sampleRate = 0.0;
            const auto rendered = readWav (out, sampleRate);
            const auto clicks = findClicks (rendered, sampleRate);
            REQUIRE (clicks.size() == 32);

            for (size_t i = 0; i < clicks.size(); ++i)
            {
                CAPTURE (i);
                CHECK (std::abs (clicks[i] - (first + apart * (double) i)) < 0.004);
            }
        };

        // A beat of the song at 100 BPM is 0.6 s: the first hit on beat 4, the others on the next.
        checkExport (*song.project, 2.4, 0.6);

        // The song faster: on its beats still.
        song.project->setTempoBpm (125.0);
        CHECK (core::clips::getStartBeat (*clip) == doctest::Approx (4.0));
        checkExport (*song.project, 1.92, 0.48);

        // The picture: under beat 4 + n of the song lies where the nth hit is in the file.
        {
            const audio_tempo::FileUnderSong under (*clip);
            CHECK_FALSE (under.isStraight());

            for (const auto n : { 0, 1, 7, 20, 31 })
            {
                CAPTURE (n);
                CHECK (std::abs (under.toFileSeconds (4.0 + n) - beats[(size_t) n]) < 0.002);
                CHECK (std::abs (under.toSongBeat (beats[(size_t) n]) - (4.0 + n)) < 0.005);
            }
        }

        // Saved and opened, it follows as it did.
        {
            const auto songFile = song.temp.getFile ("Played.harmonia");
            REQUIRE (song.project->save (songFile).isEmpty());
            juce::String error;
            auto loaded = core::Project::load (test::session(), songFile, error);
            REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

            auto* back = dynamic_cast<te::WaveAudioClip*> (core::clips::getClips (*loaded->getTracks().getLast()).getFirst());
            REQUIRE (back != nullptr);
            CHECK (audio_tempo::followsBeatByBeat (*back));
            checkExport (*loaded, 1.92, 0.48);
        }

        // Taken for twice the tempo, it is still followed beat by beat: a hit every two beats.
        const auto bpm = audio_tempo::getLoopBpm (*clip);
        REQUIRE (bpm.has_value());
        audio_tempo::setFollowsTempo (*clip, *bpm * 2.0);
        CHECK (audio_tempo::followsBeatByBeat (*clip));
        CHECK (audio_tempo::getLoopBpm (*clip).value_or (0.0) == doctest::Approx (*bpm * 2.0).epsilon (0.0001));
        checkExport (*song.project, 1.92, 0.96);

        // Its own tempo again; the sound slid later under the clip as far as its file goes: the
        // silence before the first hit shows again, at the speed the sound starts with.
        audio_tempo::setFollowsTempo (*clip, *bpm);
        CHECK (audio_tempo::slide (*clip, 100.0));
        CHECK_FALSE (audio_tempo::slide (*clip, 1.0));
        checkExport (*song.project, 1.92 + 0.48 * 0.25 / (beats[1] - beats[0]), 0.48);

        // Following off: the file at its own speed again, from its start.
        audio_tempo::setFollowsTempo (*clip, std::nullopt);
        CHECK_FALSE (audio_tempo::followsTempo (*clip));
        CHECK_FALSE (audio_tempo::followsBeatByBeat (*clip));
        CHECK (std::abs (clip->getPosition().getOffset().inSeconds()) < 1.0e-6);

        // One Ctrl+Z: as it was dropped.
        song.project->undo();
        CHECK_FALSE (audio_tempo::followsTempo (*clip));
        CHECK_FALSE (clip->state.hasProperty (te::IDs::sourceTempoMap));
        CHECK (std::abs (clip->getPosition().getOffset().inSeconds()) < 1.0e-6);
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (beats[31] + 0.5).epsilon (0.001));
    }

    TEST_CASE ("a sound followed beat by beat, played backwards: its beats are turned round with it")
    {
        AudioSong song;
        song.project->setTempoBpm (100.0);
        const auto beats = speedingUp (16, 0.25, 92.0, 104.0);
        const auto fileSeconds = beats.back() + 0.5;
        const auto file = writeClicksAt (song.temp.getFile ("Played.wav"), beats, 0.5);
        auto* clip = song.add (file, 4.0);
        song.project->getEdit().dispatchPendingUpdatesSynchronously();

        const auto measured = audio_tempo::measure (test::session().getEngine(), file, 100.0);
        REQUIRE (measured.has_value());
        REQUIRE (measured->drifts());
        audio_tempo::setFollowsTempo (*clip, *measured);
        audio_tempo::startOnBeat (*clip, *measured);
        const auto lengthBeats = core::clips::getEndBeat (*clip) - core::clips::getStartBeat (*clip);
        const auto tailBeats = 0.5 / (beats[15] - beats[14]);
        CHECK (lengthBeats == doctest::Approx (15.0 + tailBeats).epsilon (0.003));

        // Backwards: as long, in the same place; the silence after the last hit comes first, then
        // the hits, last to first, each on a beat (the file backwards has them as far from its
        // end as they were from its start).
        song.project->beginGesture ("Reverse");
        core::clips::setReversed (*clip, true);
        CHECK (core::clips::isReversed (*clip));
        CHECK (audio_tempo::followsBeatByBeat (*clip));
        CHECK (core::clips::getStartBeat (*clip) == doctest::Approx (4.0));
        CHECK (core::clips::getEndBeat (*clip) - core::clips::getStartBeat (*clip) == doctest::Approx (lengthBeats).epsilon (0.0005));

        {
            const audio_tempo::FileUnderSong under (*clip);

            for (const auto k : { 0, 1, 7, 15 })
            {
                CAPTURE (k);
                CHECK (std::abs (under.toFileSeconds (4.0 + tailBeats + k) - (fileSeconds - beats[(size_t) (15 - k)])) < 0.003);
            }
        }

        // Forwards again: as it was.
        CHECK (waitWhileReversing (*clip));
        core::clips::setReversed (*clip, false);
        CHECK (audio_tempo::followsBeatByBeat (*clip));
        CHECK (core::clips::getEndBeat (*clip) - core::clips::getStartBeat (*clip) == doctest::Approx (lengthBeats).epsilon (0.0005));

        {
            const audio_tempo::FileUnderSong under (*clip);

            for (const auto n : { 0, 3, 15 })
            {
                CAPTURE (n);
                CHECK (std::abs (under.toFileSeconds (4.0 + n) - beats[(size_t) n]) < 0.003);
            }
        }

        // A steady loop is turned round by the engine, as before.
        auto* loop = song.add (writeClickLoop (song.temp.getFile ("Clicks 100 BPM.wav"), 100.0, 8), 40.0);
        audio_tempo::setFollowsTempo (*loop, 100.0);
        core::clips::setReversed (*loop, true);
        CHECK (core::clips::isReversed (*loop));
        CHECK_FALSE (audio_tempo::followsBeatByBeat (*loop));
        CHECK (core::clips::getEndBeat (*loop) - core::clips::getStartBeat (*loop) == doctest::Approx (8.0));
        CHECK (waitWhileReversing (*loop));
    }

    TEST_CASE ("one hit played late is put on its beat by hand: the others stay where they were")
    {
        // 16 hits at 100 BPM after a quarter of a second, the seventh 40 ms late; the song at 120.
        AudioSong song;
        auto beats = speedingUp (16, 0.25, 100.0, 100.0);
        beats[6] += 0.04;
        const auto file = writeClicksAt (song.temp.getFile ("Late.wav"), beats, 0.5);
        auto* clip = song.add (file, 4.0);
        song.project->getEdit().dispatchPendingUpdatesSynchronously();

        core::beat_finder::Beat beat;
        beat.bpm = 100.0;
        beat.firstBeatSeconds = 0.25;
        audio_tempo::setFollowsTempo (*clip, beat);
        audio_tempo::startOnBeat (*clip, beat);
        CHECK_FALSE (audio_tempo::followsBeatByBeat (*clip));
        const auto endBeat = core::clips::getEndBeat (*clip);

        // How far each hit of the exported song is from its beat (beat 4 + n: 2 s + n / 2).
        const auto exportedOff = [&song]
        {
            double sampleRate = 0.0;
            const auto rendered = song.render (sampleRate);
            auto clicks = findClicks (rendered, sampleRate);
            REQUIRE (clicks.size() == 16);

            for (size_t i = 0; i < clicks.size(); ++i)
                clicks[i] -= 2.0 + 0.5 * (double) i;

            return clicks;
        };

        // As played: the seventh hit late (40 ms of the file is a third of a tenth of a second at 120).
        auto off = exportedOff();
        CHECK (off[6] > 0.028);
        CHECK (std::abs (off[5]) < 0.004);

        // Beat 10 of the song is put where that hit is in the file.
        song.project->beginGesture ("Move beat");
        CHECK (audio_tempo::moveBeat (*clip, 10.0, beats[6]));
        CHECK_FALSE (audio_tempo::moveBeat (*clip, 10.0, beats[6]));       // it is there already
        CHECK (audio_tempo::followsBeatByBeat (*clip));
        CHECK (core::clips::getStartBeat (*clip) == doctest::Approx (4.0));
        CHECK (core::clips::getEndBeat (*clip) == doctest::Approx (endBeat));

        off = exportedOff();

        for (size_t i = 0; i < off.size(); ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (off[i]) < 0.004);
        }

        // Let go: as played again, one tempo. And one Ctrl+Z each.
        song.project->beginGesture ("Reset beat");
        CHECK (audio_tempo::resetBeat (*clip, 10.0));
        CHECK_FALSE (audio_tempo::resetBeat (*clip, 10.0));
        CHECK_FALSE (audio_tempo::followsBeatByBeat (*clip));
        CHECK (audio_tempo::getLoopBpm (*clip).value_or (0.0) == doctest::Approx (100.0));

        song.project->undo();
        CHECK (audio_tempo::followsBeatByBeat (*clip));
        CHECK (std::abs (audio_tempo::FileUnderSong (*clip).toFileSeconds (10.0) - beats[6]) < 0.0005);
        song.project->undo();
        CHECK_FALSE (audio_tempo::followsBeatByBeat (*clip));

        // The hits of the file, for putting beats on: each where it is.
        const auto hits = audio_tempo::findHits (test::session().getEngine(), file);
        REQUIRE (hits.size() == 16);
        CHECK (std::abs (hits[6].seconds - beats[6]) < 0.002);

        // A clip at its own speed has no beats to move.
        audio_tempo::setFollowsTempo (*clip, std::nullopt);
        CHECK_FALSE (audio_tempo::moveBeat (*clip, 10.0, 3.0));
    }

    TEST_CASE ("clips following the tempo made one clip: it follows too, its beats where the song's were")
    {
        // Two four-beat loops at 100 BPM, one after the other in a song at 120.
        AudioSong song;
        const auto file = writeClickLoop (song.temp.getFile ("Clicks 100 BPM.wav"), 100.0, 4);
        auto* first = song.add (file, 0.0);
        auto* second = song.add (file, 4.0);
        audio_tempo::setFollowsTempo (*first, 100.0);
        audio_tempo::setFollowsTempo (*second, 100.0);
        song.project->getEdit().dispatchPendingUpdatesSynchronously();
        CHECK (core::clips::getEndBeat (*second) == doctest::Approx (8.0));

        juce::String problem;
        const auto piece = audio::consolidate::prepare (*song.track, 0.0, 8.0, problem);
        REQUIRE_MESSAGE (piece.has_value(), problem.toStdString());
        CHECK (piece->followsTempo);
        song.project->beginGesture ("Consolidate");
        auto* joined = dynamic_cast<te::WaveAudioClip*> (audio::consolidate::apply (*song.project, *piece));
        REQUIRE (joined != nullptr);
        song.project->getEdit().dispatchPendingUpdatesSynchronously();
        CHECK (core::clips::getClips (*song.track).size() == 1);
        CHECK (core::clips::getStartBeat (*joined) == doctest::Approx (0.0));
        CHECK (core::clips::getEndBeat (*joined) == doctest::Approx (8.0).epsilon (0.001));

        // The new file is at the song's tempo, and the clip knows: 120 BPM.
        CHECK (audio_tempo::followsTempo (*joined));
        CHECK (audio_tempo::getLoopBpm (*joined).value_or (0.0) == doctest::Approx (120.0).epsilon (0.001));

        double sampleRate = 0.0;
        auto rendered = song.render (sampleRate);
        checkClicks (findClicks (rendered, sampleRate), 8, 0.5);

        // The song slower: the hits are still on its beats.
        song.project->setTempoBpm (90.0);
        CHECK (core::clips::getEndBeat (*joined) == doctest::Approx (8.0).epsilon (0.001));
        rendered = song.render (sampleRate);
        const auto clicks = findClicks (rendered, sampleRate);
        REQUIRE (clicks.size() == 8);

        for (size_t i = 0; i < clicks.size(); ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (clicks[i] - (double) i * 60.0 / 90.0) < 0.005);     // stretched twice by now
        }

        joined->getOriginalFile().deleteFile();
    }

    TEST_CASE ("a loop stays on the beats through a change of the song's tempo")
    {
        // The song at 100 BPM for eight beats, at 150 from then on.
        AudioSong song;
        song.project->setTempoBpm (100.0);
        core::tempo_changes::add (song.project->getEdit(), 8.0, 150.0);
        auto* clip = song.add (writeClickLoop (song.temp.getFile ("Clicks 100 BPM.wav"), 100.0, 24));
        audio_tempo::setFollowsTempo (*clip, 100.0);
        CHECK (core::clips::getEndBeat (*clip) == doctest::Approx (24.0));

        double sampleRate = 0.0;
        const auto rendered = song.render (sampleRate);
        const auto clicks = findClicks (rendered, sampleRate);
        REQUIRE (clicks.size() == 24);

        // The stretcher is a tenth of a second behind what it is fed, and catches up after the
        // change of speed: otherwise every hit after it would be late, by a third of that.
        for (size_t i = 0; i < clicks.size(); ++i)
        {
            CAPTURE (i);
            const auto expected = i <= 8 ? 0.6 * (double) i : 4.8 + 0.4 * (double) (i - 8);
            CHECK (std::abs (clicks[i] - expected) < 0.004);
        }
    }

    TEST_CASE ("a loop following the tempo is saved with its tempo")
    {
        AudioSong song;
        auto* clip = song.add (writeClickLoop (song.temp.getFile ("Clicks 100 BPM.wav"), 100.0, 8), 4.0);
        audio_tempo::setFollowsTempo (*clip, 100.0);
        audio_tempo::setPitch (*clip, -3.0f);

        const auto file = song.temp.getFile ("Song.harmonia");
        REQUIRE (song.project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

        auto* back = dynamic_cast<te::WaveAudioClip*> (core::clips::getClips (*loaded->getTracks().getLast()).getFirst());
        REQUIRE (back != nullptr);
        CHECK (audio_tempo::followsTempo (*back));
        CHECK (audio_tempo::getLoopBpm (*back).value_or (0.0) == doctest::Approx (100.0));
        CHECK (audio_tempo::getPitch (*back) == doctest::Approx (-3.0f));
        CHECK (core::clips::getStartBeat (*back) == doctest::Approx (4.0));
        CHECK (core::clips::getEndBeat (*back) == doctest::Approx (12.0));
    }

    TEST_CASE ("a loop saved as the engine set it up plays (and exports) as Harmonia plays it once opened")
    {
        // Before, a file naming its tempo followed the song by the engine's own choice: stretched by a
        // stretcher Harmonia doesn't have, from a copy of the file made in the background (which an
        // export waits for forever), moved to the song's key.
        AudioSong song;
        auto* clip = song.add (writeClickLoop (song.temp.getFile ("Clicks 100 BPM.wav"), 100.0, 8));
        CHECK_FALSE (audio_tempo::followsTempo (*clip));                 // added: at its own speed
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (4.8));

        audio_tempo::setFollowsTempo (*clip, 100.0);
        CHECK_FALSE (clip->canUseProxy());
        clip->setUsesProxy (true);
        clip->setTimeStretchMode (te::TimeStretcher::elastiquePro);
        clip->setAutoPitch (true);

        const auto file = song.temp.getFile ("Old.harmonia");
        REQUIRE (song.project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        CHECK_FALSE (loaded->hasUnsavedChanges());

        auto* back = dynamic_cast<te::WaveAudioClip*> (core::clips::getClips (*loaded->getTracks().getLast()).getFirst());
        REQUIRE (back != nullptr);
        CHECK (audio_tempo::followsTempo (*back));
        CHECK_FALSE (back->canUseProxy());
        CHECK_FALSE (back->getAutoPitch());
        CHECK (te::TimeStretcher::canProcessFor (back->getTimeStretchMode()));

        const auto out = song.temp.getFile ("Old.wav");
        REQUIRE (audio::exportWav (loaded->getEdit(), out).isEmpty());
        double sampleRate = 0.0;
        const auto rendered = readWav (out, sampleRate);
        CHECK (findClicks (rendered, sampleRate).size() == 8);
    }

    TEST_CASE ("pitch: an octave up sounds twice as high and lasts as long")
    {
        AudioSong song;
        auto* clip = song.add (writeSine (song.temp.getFile ("Tone.wav"), 2.0, 220.0));

        song.project->beginGesture ("Pitch");
        audio_tempo::setPitch (*clip, 12.0f);
        CHECK (audio_tempo::getPitch (*clip) == doctest::Approx (12.0f));
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (2.0));

        double sampleRate = 0.0;
        const auto rendered = song.render (sampleRate);
        CHECK (crossingsPerSecond (rendered, sampleRate, 0.5, 1.5) == doctest::Approx (2.0 * 440.0).epsilon (0.03));

        audio_tempo::setPitch (*clip, 99.0f);
        CHECK (audio_tempo::getPitch (*clip) == doctest::Approx (audio_tempo::maxPitchSemitones));

        song.project->getEdit().dispatchPendingUpdatesSynchronously();
        song.project->undo();
        CHECK (audio_tempo::getPitch (*clip) == doctest::Approx (0.0f));
    }
}
