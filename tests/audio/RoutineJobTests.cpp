#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Consolidate.h"
#include "audio/Export.h"
#include "audio/ExportJob.h"
#include "audio/Freeze.h"
#include "audio/RoutineJob.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
using audio::ExportJob;

namespace
{
    /** What the counting jobs below did. */
    struct Log
    {
        int steps = 0;      // of work, by all of them
        int gone = 0;       // jobs destroyed
    };

    /** A job that is done after so many steps (at once if none), well or with a problem. */
    class Count final : public ExportJob
    {
    public:
        Count (Log& l, int steps, const juce::String& endsWith = {})
            : log (l), total (steps), left (steps), problem (endsWith)
        {
            if (left == 0)
                end();
        }

        ~Count() override
        {
            ++log.gone;
        }

        float getProgress() const override          { return total > 0 ? (float) (total - left) / (float) total : 1.0f; }
        juce::String getStage() const override      { return "Counting"; }

    private:
        Log& log;
        const int total;
        int left;
        const juce::String problem;

        bool work (double) override
        {
            ++log.steps;

            if (--left > 0)
                return false;

            end();
            return true;
        }

        void end()
        {
            error = problem;
            finished = true;
        }
    };

    /** A counting job, one that has nothing to do, and another counting job; its result is the
        steps counted. */
    class TwoCounts final : public audio::ResultJob<int>
    {
    public:
        TwoCounts (Log& l, int stepsOfFirst, const juce::String& firstEndsWith = {})
            : log (l), first (stepsOfFirst), problem (firstEndsWith)
        {
            begin (run());
        }

        ~TwoCounts() override
        {
            stop();
        }

    private:
        Log& log;
        const int first;
        const juce::String problem;

        audio::Steps run()
        {
            say ("First", 0, 2);

            {
                const auto job = std::make_unique<Count> (log, first, problem);
                co_yield *job;

                if (job->getError().isNotEmpty())
                {
                    fail (job->getError());
                    co_return;
                }
            }

            {
                const auto nothing = std::make_unique<Count> (log, 0);
                co_yield *nothing;
            }

            say ("Second", 1, 2);
            const auto job = std::make_unique<Count> (log, 2);
            co_yield *job;
            result = log.steps;
        }
    };

    //==============================================================================
    /** One audio track, "Guitar", with three seconds of a tone. */
    struct Song
    {
        Song()
        {
            track = project->getTracks().getFirst();
            REQUIRE (track != nullptr);
            track->setName ("Guitar");
            core::tracks::setKind (*track, core::tracks::Kind::audio);

            juce::String error;
            REQUIRE (project->addAudioClip (*track, test::writeTone (sounds.getFile ("Tone.wav"), 3.0, 0.3f), 0.0, error) != nullptr);
            project->clearUndoHistory();
        }

        te::TimeRange wholeClip() const
        {
            return { te::TimePosition(), te::TimePosition::fromSeconds (3.0) };
        }

        /** How many files there are where the song's recordings go (and a freeze or a
            consolidation writes). */
        int countRecordings() const
        {
            const auto folder = project->getEdit().engine.getEngineBehaviour().getFileForNewAudioRecording (*track, ".wav").getParentDirectory();
            return folder.findChildFiles (juce::File::findFiles, false).size();
        }

        /** Steps the job until it has something rendered, but not all. */
        static void startRendering (ExportJob& job)
        {
            // (The engine first lets the plugins settle for a moment: no progress yet.)
            for (int i = 0; i < 5000 && job.getProgress() <= 0.0f; ++i)
                REQUIRE (job.step (0));

            CHECK (job.getProgress() > 0.0f);
            CHECK (job.getProgress() < 1.0f);
        }

        test::TempFolder sounds, output;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* track = nullptr;
    };

    juce::AudioBuffer<float> read (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buffer (2, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer;
    }

    bool areTheSame (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
            return false;

        for (int channel = 0; channel < a.getNumChannels(); ++channel)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (a.getSample (channel, i) != b.getSample (channel, i))
                    return false;

        return true;
    }

    /** The job to its end a block at a time; how many steps it took. Its progress never goes back. */
    int finish (ExportJob& job)
    {
        int steps = 0;
        auto last = job.getProgress();

        while (job.step (0))
        {
            ++steps;
            const auto now = job.getProgress();
            CHECK (now >= last);
            CHECK (now <= 1.0f);
            last = now;
        }

        return steps;
    }
}

TEST_SUITE ("audio::RoutineJob")
{
    TEST_CASE ("a job of several jobs does them one after another, and says how far it is")
    {
        Log log;

        {
            TwoCounts job (log, 2);

            // As far as its first job at once; no work yet.
            CHECK_FALSE (job.isFinished());
            CHECK (job.getStage() == "First");
            CHECK (job.getProgress() == 0.0f);
            CHECK (log.steps == 0);

            CHECK (job.step (0));
            CHECK (job.getProgress() == doctest::Approx (0.25f));

            // The first is done: on to the second. The one in between had nothing to do, and
            // isn't waited for.
            CHECK (job.step (0));
            CHECK (log.steps == 2);
            CHECK (log.gone == 2);
            CHECK (job.getStage() == "Second");
            CHECK (job.getProgress() == doctest::Approx (0.5f));

            CHECK (job.step (0));
            CHECK (job.getProgress() == doctest::Approx (0.75f));
            CHECK_FALSE (job.isFinished());

            CHECK_FALSE (job.step (0));
            CHECK (job.isFinished());
            CHECK (job.getError().isEmpty());
            CHECK (job.getStage() == "Done");
            CHECK (job.getProgress() == 1.0f);
            CHECK (job.getResult() == 4);
            CHECK (log.gone == 3);

            // Done is done.
            CHECK_FALSE (job.step (0));
            CHECK (log.steps == 4);
        }

        CHECK (log.gone == 3);
    }

    TEST_CASE ("a job that fails ends the whole; what cannot begin has finished before any step; stopped, what it held goes")
    {
        {
            Log log;
            TwoCounts job (log, 1, "No");
            CHECK_FALSE (job.step (0));
            CHECK (job.isFinished());
            CHECK (job.getError() == "No");
            CHECK (job.getStage() == "Failed");
            CHECK (log.steps == 1);
            CHECK (log.gone == 1);
        }

        {
            Log log;
            TwoCounts job (log, 0, "Nothing to count");
            CHECK (job.isFinished());
            CHECK (job.getError() == "Nothing to count");
            CHECK_FALSE (job.step (0));
            CHECK (log.steps == 0);
        }

        // Stopped half way: the job it was waiting for goes with it, and nothing more is done.
        {
            Log log;

            {
                TwoCounts job (log, 5);
                CHECK (job.step (0));
                CHECK (log.gone == 0);
            }

            CHECK (log.gone == 1);
            CHECK (log.steps == 1);
        }
    }

    TEST_CASE ("finish does all of a job on the spot and tells each new thing it says; followedBy adds what comes after a job")
    {
        {
            Log log;
            TwoCounts job (log, 3);
            juce::StringArray said;
            job.finish ([&said] (const juce::String& stage) { said.add (stage); });
            CHECK (said == juce::StringArray { "First", "Second" });
            CHECK (job.isFinished());
            CHECK (job.getResult() == 5);
        }

        // What comes after is done in the step that ends the job.
        {
            Log log;
            int after = 0;
            const auto job = audio::followedBy (std::make_unique<Count> (log, 2), [&after] { ++after; return juce::String(); });
            CHECK (job->getStage() == "Counting");
            CHECK (job->step (0));
            CHECK (job->getProgress() == doctest::Approx (0.5f));
            CHECK (after == 0);
            CHECK_FALSE (job->step (0));
            CHECK (after == 1);
            CHECK (job->getError().isEmpty());
        }

        // Not after a job that failed; and its own problem is the job's.
        {
            Log log;
            int after = 0;
            const auto failed = audio::followedBy (std::make_unique<Count> (log, 1, "No"), [&after] { ++after; return juce::String(); });
            failed->finish();
            CHECK (failed->getError() == "No");
            CHECK (after == 0);

            const auto spoilt = audio::followedBy (std::make_unique<Count> (log, 1), [] { return juce::String ("Not this"); });
            spoilt->finish();
            CHECK (spoilt->getError() == "Not this");

            // A job that had nothing to do: all of it at once.
            const auto atOnce = audio::followedBy (ExportJob::failed ("Nothing to do"), [&after] { ++after; return juce::String(); });
            CHECK (atOnce->isFinished());
            CHECK (atOnce->getError() == "Nothing to do");
            CHECK (after == 0);
        }
    }

    //==============================================================================
    TEST_CASE ("a track rendered a bit at a time is the track rendered in one go; stopped half way, no file and the song as it was")
    {
        Song song;
        song.track->setMute (true);     // rendered all the same
        song.project->clearUndoHistory();
        const auto range = song.wholeClip();

        const auto inOneGo = song.output.getFile ("One go.wav"), bitByBit = song.output.getFile ("Bit by bit.wav");
        REQUIRE (audio::renderTrack (*song.track, inOneGo, range).isEmpty());

        auto job = ExportJob::track (*song.track, bitByBit, range);
        CHECK (job->getStage() == "Rendering Guitar");
        CHECK (finish (*job) > 10);
        CHECK_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        REQUIRE (job->getFiles().size() == 1);
        CHECK (job->getFiles().getFirst() == bitByBit);

        const auto whole = read (inOneGo);
        CHECK (whole.getMagnitude (0, 22050, 44100) > 0.2f);
        CHECK (areTheSame (whole, read (bitByBit)));

        // Its clips alone (what one clip of several is made of), the same way.
        const auto clipsInOneGo = song.output.getFile ("Clips, one go.wav"), clipsBitByBit = song.output.getFile ("Clips, bit by bit.wav");
        REQUIRE (audio::renderClips (*song.track, clipsInOneGo, range).isEmpty());
        job = ExportJob::clips (*song.track, clipsBitByBit, range);
        CHECK (finish (*job) > 10);
        CHECK (job->getError().isEmpty());
        CHECK (areTheSame (read (clipsInOneGo), read (clipsBitByBit)));

        // Stopped half way.
        const auto stopped = song.output.getFile ("Stopped.wav");
        job = ExportJob::track (*song.track, stopped, range, true);
        Song::startRendering (*job);
        CHECK_FALSE (song.track->isMuted (false));      // for the time of the render
        CHECK (song.project->getEdit().isRendering());
        job.reset();

        CHECK_FALSE (stopped.existsAsFile());
        CHECK (song.track->isMuted (false));
        CHECK_FALSE (song.project->getEdit().isRendering());
        CHECK_FALSE (song.project->canUndo());
        CHECK_FALSE (song.project->hasUnsavedChanges());
    }

    TEST_CASE ("freezing a bit at a time: the song changes at the very end, in one undo step; stopped before, the track is as it was")
    {
        Song song;
        auto& track = *song.track;
        const auto filesBefore = song.countRecordings();

        // Stopped half way: not frozen, no file, nothing to undo.
        auto job = audio::freeze::makeJob (*song.project, track);
        CHECK_FALSE (job->isFinished());
        CHECK (job->getStage() == "Freezing Guitar");
        Song::startRendering (*job);
        job.reset();

        CHECK_FALSE (audio::freeze::isFrozen (track));
        CHECK (song.countRecordings() == filesBefore);
        CHECK_FALSE (song.project->canUndo());
        REQUIRE (core::clips::getClips (track).size() == 1);
        CHECK_FALSE (core::clips::getClips (track).getFirst()->isMuted());

        // To its end.
        job = audio::freeze::makeJob (*song.project, track);
        int steps = 0;

        while (job->step (0))
        {
            ++steps;
            REQUIRE_FALSE (audio::freeze::isFrozen (track));
        }

        CHECK (steps > 10);
        CHECK_MESSAGE (job->getError().isEmpty(), job->getError().toStdString());
        CHECK (job->getStage() == "Done");
        CHECK (audio::freeze::isFrozen (track));
        REQUIRE (job->getFiles().size() == 1);
        CHECK (job->getFiles().getFirst() == audio::freeze::getFile (track));
        CHECK (song.countRecordings() == filesBefore + 1);
        CHECK (song.project->getUndoDescription() == "Freeze track");

        // Frozen already: nothing to do, and no error.
        const auto again = audio::freeze::makeJob (*song.project, track);
        CHECK (again->isFinished());
        CHECK (again->getError().isEmpty());

        song.project->getEdit().dispatchPendingUpdatesSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        song.project->undo();
        CHECK_FALSE (audio::freeze::isFrozen (track));

        // Nothing to freeze: said before any work.
        auto* empty = song.project->addTrack ("Empty");
        REQUIRE (empty != nullptr);
        const auto none = audio::freeze::makeJob (*song.project, *empty);
        CHECK (none->isFinished());
        CHECK (none->getError() == "Nothing to freeze: Empty has no clips");
    }

    TEST_CASE ("the sound of clips to make one of, rendered a bit at a time; stopped half way, no file is left")
    {
        Song song;
        const auto filesBefore = song.countRecordings();

        auto job = audio::consolidate::makePrepareJob (*song.track, 0.0, 4.0);
        CHECK_FALSE (job->isFinished());
        CHECK (job->getStage() == "Consolidating Guitar");
        Song::startRendering (*job);
        job.reset();
        CHECK (song.countRecordings() == filesBefore);
        CHECK_FALSE (song.project->hasUnsavedChanges());

        job = audio::consolidate::makePrepareJob (*song.track, 0.0, 4.0);
        CHECK (finish (*job) > 5);
        CHECK (job->getError().isEmpty());
        REQUIRE (job->getResult().has_value());
        CHECK (job->getResult()->track == song.track);
        CHECK (job->getResult()->sound.existsAsFile());
        CHECK (song.countRecordings() == filesBefore + 1);
        CHECK (read (job->getResult()->sound).getNumSamples() > 80000);      // two seconds of it

        // No clips there: nothing, and that is no error.
        const auto none = audio::consolidate::makePrepareJob (*song.track, 40.0, 44.0);
        CHECK (none->isFinished());
        CHECK_FALSE (none->getResult().has_value());
        CHECK (none->getError().isEmpty());
    }
}
