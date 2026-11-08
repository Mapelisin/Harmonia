#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Project.h"
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
