#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "audio/ExportJob.h"
#include "audio/Groups.h"
#include "audio/Loudness.h"
#include "audio/Mp3Encoder.h"
#include "audio/SongExport.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
using audio::ExportFormat;
using audio::ExportJob;
using audio::SongExport;

namespace
{
    /** Three audio tracks: Bass (a tone, 0-3 s), Keys (hits of it, 1-3 s) and Vocals (a quieter
        tone, 0-2 s). Sound files rather than the synth: two renders of them are the same to
        the sample. */
    struct Song
    {
        Song()
        {
            bass = project->getTracks().getFirst();
            keys = project->addTrack ("Keys");
            vocals = project->addTrack ("Vocals");
            REQUIRE (bass != nullptr);
            REQUIRE (keys != nullptr);
            REQUIRE (vocals != nullptr);
            bass->setName ("Bass");

            for (auto* track : { bass, keys, vocals })
                core::tracks::setKind (*track, core::tracks::Kind::audio);

            juce::String error;
            REQUIRE (project->addAudioClip (*bass, test::writeTone (sounds.getFile ("Bass.wav"), 3.0, 0.3f), 0.0, error) != nullptr);
            REQUIRE (project->addAudioClip (*keys, test::writeTone (sounds.getFile ("Keys.wav"), 2.0, 0.2f, 1, true), 2.0, error) != nullptr);
            REQUIRE (project->addAudioClip (*vocals, test::writeTone (sounds.getFile ("Vocals.wav"), 2.0, 0.1f), 0.0, error) != nullptr);
            project->clearUndoHistory();
        }

        /** The files in the folder the exports go to. */
        juce::StringArray listOutput() const
        {
            juce::StringArray names;

            for (const auto& file : output.folder.findChildFiles (juce::File::findFilesAndDirectories, true))
                names.add (file.getFileName());

            names.sort (true);
            return names;
        }

        /** The song as it is into a 24-bit WAV of that name. */
        juce::File exportPlain (const juce::String& name)
        {
            const auto file = output.getFile (name);
            const auto error = audio::exportWav (project->getEdit(), file);
            REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());
            return file;
        }

        test::TempFolder sounds, output;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* bass = nullptr;
        te::AudioTrack* keys = nullptr;
        te::AudioTrack* vocals = nullptr;
    };

    juce::AudioBuffer<float> read (const juce::File& file, double* sampleRate = nullptr)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        if (sampleRate != nullptr)
            *sampleRate = reader->sampleRate;

        juce::AudioBuffer<float> buffer (2, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer;
    }

    /** The job run to its end a block at a time; what it said it was doing on the way. */
    juce::StringArray finish (ExportJob& job)
    {
        juce::StringArray stages;
        auto last = job.getProgress();

        while (job.step (0))
        {
            stages.addIfNotAlreadyThere (job.getStage());

            // It never goes back.
            const auto now = job.getProgress();
            CHECK (now >= last);
            CHECK (now <= 1.0f);
            last = now;
        }

        return stages;
    }

    /** The most a sample of `a` differs from the same sample of `b` times `gain`: a great deal
        if they aren't as long as each other. */
    float worstDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, float gain = 1.0f)
    {
        if (a.getNumSamples() != b.getNumSamples() || a.getNumSamples() == 0)
            return 100.0f;

        auto worst = 0.0f;

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < a.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (a.getSample (channel, i) - gain * b.getSample (channel, i)));

        return worst;
    }

    /** How many times a second the sound goes through zero between two moments (twice its pitch). */
    double crossingsPerSecond (const juce::AudioBuffer<float>& sound, double sampleRate, double fromSeconds, double toSeconds)
    {
        const auto from = (int) (fromSeconds * sampleRate), to = juce::jmin ((int) (toSeconds * sampleRate), sound.getNumSamples() - 1);
        int crossings = 0;

        for (int i = from; i < to; ++i)
            if ((sound.getSample (0, i) < 0.0f) != (sound.getSample (0, i + 1) < 0.0f))
                ++crossings;

        return crossings / (toSeconds - fromSeconds);
    }

    float rmsBetween (const juce::AudioBuffer<float>& sound, double sampleRate, double fromSeconds, double toSeconds)
    {
        return sound.getRMSLevel (0, (int) (fromSeconds * sampleRate), (int) ((toSeconds - fromSeconds) * sampleRate));
    }

    audio::loudness::Measurement measure (const juce::File& file)
    {
        const auto measured = audio::loudness::measureFile (file);
        REQUIRE (measured.has_value());
        return *measured;
    }

    bool isNear (double a, double b, double tolerance)
    {
        return std::abs (a - b) <= tolerance;
    }
}

TEST_SUITE ("audio::SongExport")
{
    TEST_CASE ("the song at another sample rate: a file of that rate, as long, the same notes as loud")
    {
        Song song;
        auto& edit = song.project->getEdit();
        const auto deviceRate = edit.engine.getDeviceManager().getSampleRate();

        auto plainRate = 0.0;
        const auto plain = read (song.exportPlain ("Plain.wav"), &plainRate);
        CHECK (plainRate == deviceRate);
        const auto seconds = plain.getNumSamples() / plainRate;
        const auto rms = rmsBetween (plain, plainRate, 0.3, 0.9);
        CHECK (rms > 0.05f);

        for (const auto rate : audio::exportSampleRates)
        {
            CAPTURE (rate);
            SongExport how;
            how.sampleRate = rate;
            const auto file = song.output.getFile ("At " + juce::String ((int) rate) + ".wav");
            const auto error = audio::exportSong (edit, file, how);
            REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

            auto fileRate = 0.0;
            const auto sound = read (file, &fileRate);
            CHECK (fileRate == rate);
            CHECK (sound.getNumSamples() / fileRate == doctest::Approx (seconds).epsilon (0.002));

            // The bass and the voice in the first second: 220 Hz, as loud as at the device's rate.
            CHECK (crossingsPerSecond (sound, fileRate, 0.3, 0.9) == doctest::Approx (440.0).epsilon (0.01));
            CHECK (rmsBetween (sound, fileRate, 0.3, 0.9) == doctest::Approx (rms).epsilon (0.02));
        }

        // A FLAC, and a part of the song, at a rate asked for.
        SongExport flac (ExportFormat { ExportFormat::Type::flac, 24 });
        flac.sampleRate = 48000.0;
        const auto flacFile = song.output.getFile ("Song.flac"), partFile = song.output.getFile ("Part.flac");
        REQUIRE (audio::exportSong (edit, flacFile, flac).isEmpty());
        REQUIRE (audio::exportSong (edit, partFile, flac, te::TimeRange (te::TimePosition::fromSeconds (0.5), te::TimePosition::fromSeconds (1.5))).isEmpty());

        auto flacRate = 0.0, partRate = 0.0;
        const auto flacSound = read (flacFile, &flacRate), part = read (partFile, &partRate);
        CHECK (flacRate == 48000.0);
        CHECK (flacSound.getNumSamples() / flacRate == doctest::Approx (seconds).epsilon (0.002));
        CHECK (partRate == 48000.0);
        CHECK (part.getNumSamples() == 48000);

        // An MP3 can't have more than 48 kHz: the nearest it can.
        const ExportFormat mp3 { ExportFormat::Type::mp3, 16, 192 };
        CHECK (audio::getFileSampleRate (mp3, 96000.0, 44100.0) == 48000.0);
        CHECK (audio::getFileSampleRate (mp3, 88200.0, 48000.0) == 44100.0);
        CHECK (audio::getFileSampleRate (mp3, 48000.0, 44100.0) == 48000.0);
        CHECK (audio::getFileSampleRate (mp3, 0.0, 96000.0) == 48000.0);
        CHECK (audio::getFileSampleRate (mp3, 0.0, 44100.0) == 44100.0);
        CHECK (audio::getFileSampleRate ({}, 96000.0, 44100.0) == 96000.0);
        CHECK (audio::getFileSampleRate ({}, 0.0, 44100.0) == 44100.0);

        if (audio::mp3::isAvailable (192))
        {
            SongExport how (mp3);
            how.sampleRate = 96000.0;
            const auto file = song.output.getFile ("Song.mp3");
            const auto error = audio::exportSong (edit, file, how);
            REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

            auto mp3Rate = 0.0;
            const auto sound = read (file, &mp3Rate);
            CHECK (mp3Rate == 48000.0);
            CHECK (sound.getNumSamples() / mp3Rate == doctest::Approx (seconds).epsilon (0.05));
        }

        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
    }

    TEST_CASE ("the song brought to a loudness: the file measures it, its peaks stay under the ceiling, the song is as it was")
    {
        Song song;
        auto& edit = song.project->getEdit();
        const auto plainFile = song.exportPlain ("Plain.wav");
        const auto plain = read (plainFile);
        const auto before = measure (plainFile);
        const auto room = audio::loudness::peakCeilingDb - before.peakDb;
        REQUIRE (room > 3.0f);      // the song has room over its peaks: the cases below need it

        // Six decibels down: every sample half of what it was.
        SongExport how;
        how.loudnessLufs = before.lufs - 6.0;
        const auto quieterFile = song.output.getFile ("Quieter.wav");
        auto job = ExportJob::song (edit, quieterFile, how);
        CHECK (job->getNote().isEmpty());
        const auto stages = finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (stages == juce::StringArray ({ "Rendering the song", "Measuring how loud it is", "Writing Quieter.wav" }));
        CHECK (job->getNote() == juce::String (before.lufs - 6.0, 1) + " LUFS (lowered by 6.0 dB)");
        CHECK (job->getFiles().size() == 1);

        const auto quieter = measure (quieterFile);
        CHECK (isNear (quieter.lufs, before.lufs - 6.0, 0.1));
        CHECK (isNear (quieter.peakDb, before.peakDb - 6.0, 0.1));
        CHECK (worstDifference (read (quieterFile), plain, juce::Decibels::decibelsToGain (-6.0f)) < 0.0001f);

        // Two up: there is room for it.
        how.loudnessLufs = before.lufs + 2.0;
        const auto louderFile = song.output.getFile ("Louder.wav");
        job = ExportJob::song (edit, louderFile, how);
        finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (job->getNote() == juce::String (before.lufs + 2.0, 1) + " LUFS (raised by 2.0 dB)");
        CHECK (isNear (measure (louderFile).lufs, before.lufs + 2.0, 0.1));

        // Twenty up would clip: as far as the peaks allow, and it says so.
        how.loudnessLufs = before.lufs + 20.0;
        const auto loudestFile = song.output.getFile ("Loudest.wav");
        job = ExportJob::song (edit, loudestFile, how);
        finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        const auto loudest = measure (loudestFile);
        CHECK (isNear (loudest.peakDb, audio::loudness::peakCeilingDb, 0.05));
        CHECK (isNear (loudest.lufs, before.lufs + room, 0.1));
        CHECK (job->getNote().startsWith (juce::String (before.lufs + room, 1) + " LUFS, not " + juce::String (before.lufs + 20.0, 0)));
        CHECK (job->getNote().contains ("raised by " + juce::String (room, 1) + " dB, as far as its peaks allow"));

        // Already there: the file is the song as it is.
        how.loudnessLufs = before.lufs;
        const auto sameFile = song.output.getFile ("Same.wav");
        job = ExportJob::song (edit, sameFile, how);
        finish (*job);
        CHECK (job->getNote().endsWith ("LUFS (as loud as the song is)"));
        CHECK (worstDifference (read (sameFile), plain) < 0.0001f);

        // In the other kinds of file too (a 16-bit one gets its dither as it is written).
        how.loudnessLufs = before.lufs - 6.0;

        for (const auto& format : { ExportFormat { ExportFormat::Type::wav, 16 }, ExportFormat { ExportFormat::Type::flac, 24 },
                                    ExportFormat { ExportFormat::Type::mp3, 16, 192 } })
        {
            if (format.type == ExportFormat::Type::mp3 && ! audio::mp3::isAvailable (format.kbps))
                continue;

            CAPTURE (format.describe());
            how.format = format;
            const auto file = song.output.getFile ("Quieter" + format.toString() + format.getExtension());
            const auto error = audio::exportSong (edit, file, how);
            REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());
            CHECK (isNear (measure (file).lufs, before.lufs - 6.0, format.type == ExportFormat::Type::mp3 ? 0.5 : 0.1));

            if (format.type != ExportFormat::Type::mp3)
                CHECK (worstDifference (read (file), plain, juce::Decibels::decibelsToGain (-6.0f)) < 0.001f);
        }

        // A part of the song is a sample: as it sounds, whatever loudness was asked for.
        how.format = {};
        const auto partFile = song.output.getFile ("Part.wav");
        job = ExportJob::song (edit, partFile, how, te::TimeRange (te::TimePosition::fromSeconds (0.5), te::TimePosition::fromSeconds (1.5)));
        const auto partStages = finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK_FALSE (partStages.contains ("Measuring how loud it is"));
        CHECK (job->getNote().isEmpty());
        const auto part = read (partFile);
        CHECK (part.getRMSLevel (0, 0, part.getNumSamples()) == doctest::Approx (rmsBetween (plain, 44100.0, 0.5, 1.5)).epsilon (0.01));

        // Stopped before the file is written: nothing of it stays.
        const auto filesBefore = song.listOutput();
        job = ExportJob::song (edit, song.output.getFile ("Stopped.wav"), how);

        while (job->getStage() == "Rendering the song")
            REQUIRE (job->step (0));

        CHECK (job->getStage() == "Measuring how loud it is");
        job.reset();
        CHECK (song.listOutput() == filesBefore);

        // The song itself was never turned up or down.
        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
        CHECK (worstDifference (read (song.exportPlain ("Plain again.wav")), plain) < 0.0001f);
    }

    TEST_CASE ("a second file without a track: the song as it sounds with that track muted, named after it")
    {
        Song song;
        auto& edit = song.project->getEdit();
        const auto plain = read (song.exportPlain ("Plain.wav"));

        SongExport how;
        how.without.add (song.vocals);
        const auto file = song.output.getFile ("My song.wav"), minus = song.output.getFile ("My song (no Vocals).wav");
        CHECK (audio::getMinusFile (file, how.without) == minus);
        CHECK (audio::getMinusSuffix (how.without) == " (no Vocals)");

        auto job = ExportJob::song (edit, file, how);
        CHECK_FALSE (job->isFinished());
        CHECK (job->getNumParts() == 2);
        CHECK (job->getNumPartsDone() == 0);
        CHECK (job->getStage() == "Rendering the song");
        const auto stages = finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (stages == juce::StringArray ({ "Rendering the song", "Writing My song.wav",
                                              "Rendering the song without Vocals", "Writing My song (no Vocals).wav" }));
        CHECK (job->getNumPartsDone() == 2);
        REQUIRE (job->getFiles().size() == 2);
        CHECK (job->getFiles()[0] == file);
        CHECK (job->getFiles()[1] == minus);
        CHECK (job->getNote().isEmpty());
        job.reset();

        CHECK (song.listOutput() == juce::StringArray ({ "My song (no Vocals).wav", "My song.wav", "Plain.wav" }));
        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
        CHECK_FALSE (song.vocals->isMuted (false));

        // The first is the song; the second what it is with the voice muted, to the sample.
        CHECK (worstDifference (read (file), plain) < 0.0001f);

        song.vocals->setMute (true);
        const auto withoutVocals = read (song.exportPlain ("Muted.wav"));
        song.vocals->setMute (false);
        CHECK (worstDifference (read (minus), withoutVocals) < 0.0001f);
        CHECK (worstDifference (withoutVocals, plain) > 0.01f);

        // Brought to a loudness, the second is turned as the first was: still the same mix.
        how.loudnessLufs = audio::loudness::measureFile (song.output.getFile ("Plain.wav")).value_or (audio::loudness::Measurement()).lufs - 6.0;
        const auto quietFile = song.output.getFile ("Quiet.wav");
        job = ExportJob::song (edit, quietFile, how);
        finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (job->getNote().endsWith ("LUFS (lowered by 6.0 dB)"));
        const auto half = juce::Decibels::decibelsToGain (-6.0f);
        CHECK (worstDifference (read (quietFile), plain, half) < 0.0001f);
        CHECK (worstDifference (read (audio::getMinusFile (quietFile, how.without)), withoutVocals, half) < 0.0001f);
        how.loudnessLufs.reset();

        // Several tracks left out: "minus".
        how.without.add (song.keys);
        const auto bandFile = song.output.getFile ("Band.wav"), bassFile = song.output.getFile ("Band (minus).wav");
        CHECK (audio::getMinusFile (bandFile, how.without) == bassFile);
        job = ExportJob::song (edit, bandFile, how);
        const auto bandStages = finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (bandStages.contains ("Rendering the song without 2 tracks"));

        song.vocals->setMute (true);
        song.keys->setMute (true);
        const auto bassOnly = read (song.exportPlain ("Bass only.wav"));
        song.vocals->setMute (false);
        song.keys->setMute (false);
        CHECK (worstDifference (read (bassFile), bassOnly) < 0.0001f);

        // A group left out: all that plays through it.
        auto* group = audio::groups::addGroup (*song.project, { song.keys, song.vocals }, "Top");
        REQUIRE (group != nullptr);
        how.without = { group };
        const auto groupFile = song.output.getFile ("Grouped.wav");
        CHECK (audio::getMinusFile (groupFile, how.without).getFileName() == "Grouped (no Top).wav");
        job = ExportJob::song (edit, groupFile, how);
        finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (worstDifference (read (audio::getMinusFile (groupFile, how.without)), bassOnly) < 0.0001f);

        // A name a file can't have is made one that it can.
        song.vocals->setName ("Lead / Back?");
        how.without = { song.vocals };
        CHECK (audio::getMinusSuffix (how.without) == " (no Lead  Back)");
        song.vocals->setName ("?*");       // nothing of it is left
        CHECK (audio::getMinusSuffix (how.without) == " (minus)");
    }

    TEST_CASE ("a second file that can't be made says so before any work; stopped half way, neither file stays")
    {
        Song song;
        auto& edit = song.project->getEdit();

        // Nothing would be left to hear.
        SongExport how;
        how.without = { song.bass, song.keys, song.vocals };
        auto job = ExportJob::song (edit, song.output.getFile ("Song.wav"), how);
        CHECK (job->isFinished());
        CHECK (job->getError() == "Nothing to export: without 3 tracks no track of the song is heard");
        CHECK (job->getFiles().isEmpty());
        job.reset();
        CHECK (song.listOutput().isEmpty());

        // A song with nothing in it.
        const auto empty = core::Project::createEmpty (test::session());
        job = ExportJob::song (empty->getEdit(), song.output.getFile ("Empty.wav"), how);
        CHECK (job->isFinished());
        CHECK (job->getError().contains ("the song has no clips"));

        // A track that isn't the song's (it went while the file was being chosen): the song alone.
        how.without = { empty->getTracks().getFirst() };
        job = ExportJob::song (edit, song.output.getFile ("Alone.wav"), how);
        CHECK (job->getNumParts() == 1);
        finish (*job);
        CHECK (job->getError().isEmpty());
        CHECK (job->getFiles().size() == 1);
        job.reset();
        CHECK (song.listOutput() == juce::StringArray ({ "Alone.wav" }));

        // Stopped while the second file is rendered: the first, already written, goes too.
        how.without = { song.vocals };
        const auto file = song.output.getFile ("Stopped.wav");
        job = ExportJob::song (edit, file, how);

        while (job->getNumPartsDone() == 0)
            REQUIRE (job->step (0));

        CHECK (file.existsAsFile());

        for (int i = 0; i < 5000 && job->getProgress() <= 0.5f; ++i)
            REQUIRE (job->step (0));

        CHECK (job->getStage() == "Rendering the song without Vocals");
        CHECK (edit.isRendering());
        job.reset();
        CHECK (song.listOutput() == juce::StringArray ({ "Alone.wav" }));
        CHECK_FALSE (edit.isRendering());
        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
        CHECK_FALSE (song.vocals->isMuted (false));
    }

    TEST_CASE ("the sample rate and the loudness of the last export are kept with its format")
    {
        auto& session = test::session();
        const auto before = audio::getLastSongExport (session);

        SongExport how (ExportFormat { ExportFormat::Type::flac, 24 });
        how.sampleRate = 48000.0;
        how.loudnessLufs = -16.0;
        audio::setLastSongExport (session, how);

        auto last = audio::getLastSongExport (session);
        CHECK (last.format == how.format);
        CHECK (last.sampleRate == 48000.0);
        CHECK (last.loudnessLufs == std::optional<double> (-16.0));
        CHECK (last.without.isEmpty());
        CHECK (audio::getLastExportFormat (session) == how.format);

        // Back to the song as it is.
        audio::setLastSongExport (session, SongExport());
        last = audio::getLastSongExport (session);
        CHECK (last.format == ExportFormat());
        CHECK (last.sampleRate == 0.0);
        CHECK_FALSE (last.loudnessLufs.has_value());

        // What the export doesn't offer isn't taken from the settings.
        how.sampleRate = 12345.0;
        how.loudnessLufs = -3.0;
        audio::setLastSongExport (session, how);
        last = audio::getLastSongExport (session);
        CHECK (last.sampleRate == 0.0);
        CHECK_FALSE (last.loudnessLufs.has_value());

        audio::setLastSongExport (session, before);
    }
}
