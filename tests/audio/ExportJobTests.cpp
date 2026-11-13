#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "audio/ExportJob.h"
#include "audio/Groups.h"
#include "audio/Mp3Encoder.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
using audio::ExportFormat;
using audio::ExportJob;

namespace
{
    /** Two audio tracks, a tone on each (0–2 s and 1–3 s). Sound files rather than the synth: two
        renders of them are the same to the sample. */
    struct Song
    {
        Song()
        {
            first = project->getTracks().getFirst();
            second = project->addTrack ("Second");
            REQUIRE (first != nullptr);
            REQUIRE (second != nullptr);
            first->setName ("First");
            core::tracks::setKind (*first, core::tracks::Kind::audio);
            core::tracks::setKind (*second, core::tracks::Kind::audio);

            juce::String error;
            REQUIRE (project->addAudioClip (*first, test::writeTone (sounds.getFile ("A.wav"), 2.0, 0.3f), 0.0, error) != nullptr);
            REQUIRE (project->addAudioClip (*second, test::writeTone (sounds.getFile ("B.wav"), 2.0, 0.2f, 1, true), 2.0, error) != nullptr);
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

        test::TempFolder sounds, output;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* first = nullptr;
        te::AudioTrack* second = nullptr;
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
}

TEST_SUITE ("audio::ExportJob")
{
    TEST_CASE ("an export done a bit at a time writes the same file as one done in one go, and says how far it is")
    {
        Song song;
        auto& edit = song.project->getEdit();
        const auto whole = song.output.getFile ("Whole.wav"), stepped = song.output.getFile ("Stepped.wav");
        REQUIRE (audio::exportWav (edit, whole).isEmpty());

        auto job = ExportJob::song (edit, stepped, {});
        REQUIRE (job != nullptr);
        CHECK_FALSE (job->isFinished());
        CHECK (job->getProgress() == 0.0f);
        CHECK (job->getStage() == "Rendering the song");
        CHECK (job->getFiles().isEmpty());
        CHECK (job->getNumParts() == 1);
        CHECK (job->getNumPartsDone() == 0);

        // A step is short: many of them make the song.
        auto steps = 0;
        auto last = 0.0f;
        juce::StringArray stages;

        while (job->step (0))
        {
            ++steps;
            stages.addIfNotAlreadyThere (job->getStage());
            CHECK (job->getProgress() >= last);
            last = job->getProgress();
        }

        CHECK (steps > 20);
        CHECK (stages.contains ("Rendering the song"));
        CHECK (stages.contains ("Writing Stepped.wav"));

        CHECK (job->isFinished());
        CHECK_FALSE (job->step (0));
        CHECK_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (job->getProgress() == 1.0f);
        CHECK (job->getStage() == "Done");
        CHECK (job->getNumPartsDone() == 1);
        REQUIRE (job->getFiles().size() == 1);
        CHECK (job->getFiles().getFirst() == stepped);

        const auto a = read (whole), b = read (stepped);
        REQUIRE (a.getNumSamples() == b.getNumSamples());
        CHECK (a.getNumSamples() > 3 * 44100);

        auto worst = 0.0f;

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < a.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (a.getSample (channel, i) - b.getSample (channel, i)));

        CHECK (worst < 0.0001f);
        CHECK (a.getMagnitude (0, 44100) > 0.05f);

        // Only the two files; nothing to undo, nothing to save.
        job.reset();
        CHECK (song.listOutput() == juce::StringArray ({ "Stepped.wav", "Whole.wav" }));
        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
        CHECK_FALSE (edit.isRendering());
    }

    TEST_CASE ("a part of the song, a FLAC and an MP3 a bit at a time")
    {
        Song song;
        auto& edit = song.project->getEdit();

        // A second of it, exactly, as a FLAC.
        const auto flac = song.output.getFile ("Part.flac");
        auto job = ExportJob::song (edit, flac, ExportFormat { ExportFormat::Type::flac, 16 },
                                    te::TimeRange (te::TimePosition::fromSeconds (0.5), te::TimePosition::fromSeconds (1.5)));
        CHECK (job->getStage() == "Rendering the part of the song");
        finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        auto sampleRate = 0.0;
        const auto part = read (flac, &sampleRate);
        CHECK (part.getNumSamples() / sampleRate == doctest::Approx (1.0).epsilon (0.002));
        CHECK (part.getMagnitude (0, 1000) > 0.05f);    // the tone already sounds at its start

        // A part that is empty: said at once.
        job = ExportJob::song (edit, song.output.getFile ("Empty.wav"), {},
                               te::TimeRange (te::TimePosition::fromSeconds (1.0), te::TimePosition::fromSeconds (1.0)));
        CHECK (job->isFinished());
        CHECK (job->getError().contains ("Nothing to export"));

        if (audio::mp3::isAvailable (192))
        {
            const ExportFormat mp3Format { ExportFormat::Type::mp3, 16, 192 };

            // The song: the MP3 is written after the rendering, and the WAV it is made of goes.
            const auto mp3 = song.output.getFile ("Song.mp3");
            job = ExportJob::song (edit, mp3, mp3Format);
            const auto stages = finish (*job);
            REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
            CHECK (stages.contains ("Writing Song.mp3"));
            CHECK (mp3.getSize() > 10000);

            // A part of it.
            const auto loop = song.output.getFile ("Loop.mp3");
            job = ExportJob::song (edit, loop, mp3Format,
                                   te::TimeRange (te::TimePosition::fromSeconds (0.0), te::TimePosition::fromSeconds (2.0)));
            const auto loopStages = finish (*job);
            REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
            CHECK (loopStages.contains ("Writing Loop.mp3"));
            CHECK (loop.getSize() > 10000);
            CHECK (loop.getSize() < mp3.getSize());

            job.reset();
            CHECK (song.listOutput() == juce::StringArray ({ "Loop.mp3", "Part.flac", "Song.mp3" }));
        }

        CHECK_FALSE (song.project->hasUnsavedChanges());
    }

    TEST_CASE ("an export stopped half way leaves nothing: no file, no temporary one, the song as it was")
    {
        Song song;
        auto& edit = song.project->getEdit();
        song.second->setSolo (true);
        song.project->clearUndoHistory();

        for (const auto& format : { ExportFormat {}, ExportFormat { ExportFormat::Type::flac, 24 } })
        {
            // The song, stopped while it is rendered.
            auto job = ExportJob::song (edit, song.output.getFile ("Song" + format.getExtension()), format);

            // (The engine first lets the plugins settle for a moment: no progress yet.)
            for (int i = 0; i < 5000 && job->getProgress() <= 0.0f; ++i)
                REQUIRE (job->step (0));

            CHECK (job->getProgress() > 0.0f);
            CHECK (job->getProgress() < 1.0f);
            CHECK (edit.isRendering());
            job.reset();

            CHECK (song.listOutput().isEmpty());
            CHECK_FALSE (edit.isRendering());

            // A part of it, which is rendered straight into its file.
            job = ExportJob::song (edit, song.output.getFile ("Part" + format.getExtension()), format,
                                   te::TimeRange (te::TimePosition::fromSeconds (0.0), te::TimePosition::fromSeconds (3.0)));

            for (int i = 0; i < 30; ++i)
                REQUIRE (job->step (0));

            job.reset();
            CHECK (song.listOutput().isEmpty());
        }

        // Stopped after the rendering, before the file is written.
        auto job = ExportJob::song (edit, song.output.getFile ("Late.wav"), {});

        while (job->getStage() == "Rendering the song")
            REQUIRE (job->step (0));

        CHECK (job->getStage() == "Writing Late.wav");
        job.reset();
        CHECK (song.listOutput().isEmpty());

        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
        CHECK (song.second->isSolo (false));
        CHECK_FALSE (song.first->isMuted (false));

        // And the next export is none the worse for it.
        const auto file = song.output.getFile ("After.wav");
        REQUIRE (audio::exportWav (edit, file).isEmpty());
        CHECK (read (file).getMagnitude (44100, 44100) > 0.02f);
    }

    TEST_CASE ("stems a bit at a time: it names the stem it is at; stopped, it takes back what it wrote and did to the song")
    {
        Song song;
        auto& edit = song.project->getEdit();
        // Both in a group: for the stem of one, the other is silenced meanwhile.
        REQUIRE (audio::groups::addGroup (*song.project, { song.first, song.second }, "Band") != nullptr);
        song.project->clearUndoHistory();

        audio::StemsOptions options;
        options.songName = "Song";

        auto job = ExportJob::stems (edit, song.output.folder, options);
        REQUIRE (job != nullptr);
        CHECK_FALSE (job->isFinished());
        CHECK (job->getNumParts() == 2);
        CHECK (job->getNumPartsDone() == 0);
        CHECK (job->getStage() == "Stem 1 of 2: First");

        REQUIRE (job->step (0));
        CHECK (song.second->isMuted (false));       // why nobody may touch the song meanwhile

        const auto stages = finish (*job);
        REQUIRE_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (stages.contains ("Stem 1 of 2: First"));
        CHECK (stages.contains ("Stem 2 of 2: Second"));
        CHECK (stages.contains ("Writing the files"));
        CHECK (stages.indexOf ("Stem 2 of 2: Second") < stages.indexOf ("Writing the files"));
        CHECK (job->getNumPartsDone() == 2);
        CHECK (job->getProgress() == 1.0f);
        REQUIRE (job->getFiles().size() == 2);
        CHECK (job->getFiles()[0].getFileName() == "Song - 01 First.wav");
        CHECK (job->getFiles()[1].getFileName() == "Song - 02 Second.wav");

        // Each has its own track only, and they are as long as each other.
        const auto firstStem = read (job->getFiles()[0]), secondStem = read (job->getFiles()[1]);
        CHECK (firstStem.getNumSamples() == secondStem.getNumSamples());
        CHECK (firstStem.getMagnitude (4410, 30000) > 0.05f);
        CHECK (secondStem.getMagnitude (4410, 30000) < 0.001f);
        CHECK (secondStem.getMagnitude (44100 + 4410, 30000) > 0.02f);

        job.reset();
        CHECK (song.listOutput().size() == 2);
        CHECK_FALSE (song.second->isMuted (false));
        CHECK_FALSE (song.project->hasUnsavedChanges());

        for (const auto& file : song.output.folder.findChildFiles (juce::File::findFiles, false))
            REQUIRE (file.deleteFile());

        // Stopped during the first stem.
        job = ExportJob::stems (edit, song.output.folder, options);

        for (int i = 0; i < 30; ++i)
            REQUIRE (job->step (0));

        CHECK (song.second->isMuted (false));
        job.reset();
        CHECK_FALSE (song.second->isMuted (false));
        CHECK (song.listOutput().isEmpty());

        // Stopped when one of the two files is written.
        job = ExportJob::stems (edit, song.output.folder, options);

        while (job->getFiles().isEmpty())
            REQUIRE (job->step (0));

        CHECK (job->getStage() == "Writing the files");
        CHECK (song.listOutput().contains ("Song - 01 First.wav"));
        job.reset();
        CHECK (song.listOutput().isEmpty());

        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
        CHECK_FALSE (edit.isRendering());
    }

    TEST_CASE ("what cannot be exported says so before any work")
    {
        test::TempFolder output;
        auto empty = core::Project::createEmpty (test::session());

        auto job = ExportJob::song (empty->getEdit(), output.getFile ("Song.wav"), {});
        CHECK (job->isFinished());
        CHECK_FALSE (job->step (0));
        CHECK (job->getError() == "Nothing to export: the song has no clips");
        CHECK (job->getStage() == "Failed");
        CHECK (job->getFiles().isEmpty());

        job = ExportJob::stems (empty->getEdit(), output.folder, {});
        CHECK (job->isFinished());
        CHECK (job->getError() == "Nothing to export: the song has no clips");

        Song song;
        song.first->setMute (true);
        song.second->setMute (true);
        job = ExportJob::song (song.project->getEdit(), output.getFile ("Song.wav"), {});
        CHECK (job->isFinished());
        CHECK (job->getError() == "Nothing to export: every track is muted");

        job = ExportJob::stems (song.project->getEdit(), output.folder, {});
        CHECK (job->isFinished());
        CHECK (job->getError().contains ("Nothing to export"));
        CHECK (output.folder.findChildFiles (juce::File::findFiles, false).isEmpty());
    }
}
