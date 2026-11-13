#include "audio/SongExport.h"
#include "audio/ExportJob.h"
#include "audio/ExportRender.h"
#include "audio/ExportTail.h"
#include "audio/Groups.h"
#include "audio/Loudness.h"
#include "audio/Mp3Encoder.h"
#include "audio/RoutineJob.h"

namespace harmonia::audio
{
    using namespace export_render;

    namespace
    {
        const char* const sampleRateKey = "exportSampleRate";
        const char* const loudnessKey = "exportLoudness";

        /** How the level of what was rendered is set as its file is written. */
        struct Level
        {
            std::optional<double> lufs;     // brought to this loudness
            std::optional<float> gainDb;    // or turned by this much, as another file of the song was

            bool isSet() const noexcept     { return lufs.has_value() || gainDb.has_value(); }
        };

        /** What an export of the song plays, the tracks in `without` (and all that plays through
            the groups in it) left out. */
        Audible findHeard (te::Edit& edit, const juce::Array<te::AudioTrack*>& without)
        {
            auto audible = findAudible (edit);

            for (int i = audible.songTracks.size(); --i >= 0;)
            {
                auto* track = audible.songTracks.getUnchecked (i);
                auto* group = groups::getGroup (*track);

                if (without.contains (track) || (group != nullptr && without.contains (group)))
                    audible.songTracks.remove (i);
            }

            for (auto* track : without)
                audible.groupTracks.removeAllInstancesOf (track);

            return audible;
        }

        juce::String describeWithout (const juce::Array<te::AudioTrack*>& without)
        {
            return without.size() == 1 ? without.getFirst()->getName() : juce::String (without.size()) + " tracks";
        }

        /** What the user is told of a file brought to a loudness: where it is now and how it got there. */
        juce::String describeLevel (double lufs, float gainDb, double targetLufs, bool peakLimited)
        {
            const auto now = juce::String (lufs + gainDb, 1) + " LUFS";
            const auto change = std::abs (gainDb) < 0.05f ? juce::String ("as loud as the song is")
                                                          : (gainDb > 0.0f ? "raised by " : "lowered by ") + juce::String (std::abs (gainDb), 1) + " dB";

            if (! peakLimited)
                return now + " (" + change + ")";

            return now + ", not " + juce::String (targetLufs, 0) + ": " + change + ", as far as its peaks allow (the limiter of "
                       + "Check loudness takes the song all the way)";
        }

        //==============================================================================
        /** The song, or a part of it, into one file. */
        class SongJob final : public ExportJob
        {
        public:
            SongJob (te::Edit& e, const juce::File& f, const ExportFormat& format, double sampleRate, std::optional<te::TimeRange> part,
                     const Level& wanted = {}, const juce::Array<te::AudioTrack*>& without = {})
                : edit (e), file (f), level (wanted),
                  what (part.has_value() ? juce::String ("the part of the song")
                                         : without.isEmpty() ? juce::String ("the song") : "the song without " + describeWithout (without))
            {
                if (edit.getLength() <= te::TimeDuration())
                {
                    end ("Nothing to export: the song has no clips");
                    return;
                }

                if (part.has_value() && (part->getLength() <= te::TimeDuration() || part->getStart() < te::TimePosition()))
                {
                    end ("Nothing to export: the part to export is empty");
                    return;
                }

                // What you hear is what you get.
                const auto audible = findHeard (edit, without);

                if (audible.songTracks.isEmpty())
                {
                    end (without.isEmpty() ? juce::String ("Nothing to export: every track is muted")
                                           : "Nothing to export: without " + describeWithout (without) + " no track of the song is heard");
                    return;
                }

                auto tracks = audible.others;
                juce::Array<te::AudioTrack*> heard;

                for (const auto* list : { &audible.songTracks, &audible.groupTracks, &audible.returnTracks })
                    heard.addArray (*list);

                for (auto* track : heard)
                    tracks.add (track);

                // A muted kick still ducks the bass, as it does while playing (and so does a
                // voice left out of this file).
                temporary = std::make_unique<TemporaryState> (edit);
                const auto sources = keepSources (*temporary, heard, tracks);

                // A part of the song: exactly that. The song: until it has rung out.
                if (part.has_value())
                    exact = std::make_unique<Render> (edit, file, tracks, *part, true, sources, format, sampleRate);
                else
                    pastEnd = std::make_unique<PastEndRender> (edit, file, tracks, true, sources, format, sampleRate, level.isSet());
            }

            ~SongJob() override
            {
                release();
            }

            float getProgress() const override
            {
                if (finished)
                    return 1.0f;

                if (exact != nullptr)
                    return exact->getProgress();

                switch (stage)
                {
                    case Stage::measuring:  return 0.96f;
                    case Stage::writing:    return 0.97f;
                    case Stage::rendering:  break;
                }

                return pastEnd != nullptr ? 0.95f * pastEnd->getProgress() : 0.0f;
            }

            juce::String getStage() const override
            {
                if (finished)
                    return error.isEmpty() ? "Done" : "Failed";

                if (stage == Stage::measuring)
                    return "Measuring how loud it is";

                if (stage == Stage::writing || (exact != nullptr && exact->isEncoding()))
                    return "Writing " + file.getFileName();

                return "Rendering " + what;
            }

            /** How much the file was turned up or down as it was written (once it has finished). */
            float getGainDb() const noexcept            { return gainDb; }

        private:
            enum class Stage { rendering, measuring, writing };

            te::Edit& edit;
            const juce::File file;
            const Level level;
            const juce::String what;
            std::unique_ptr<TemporaryState> temporary;      // put back after the renders are gone
            std::unique_ptr<Render> exact;
            std::unique_ptr<PastEndRender> pastEnd;
            Stage stage = Stage::rendering;
            float gainDb = 0.0f;

            bool work (double untilMs) override
            {
                if (exact != nullptr)
                    return exact->run (untilMs) && end (exact->getError());

                switch (stage)
                {
                    case Stage::rendering:
                        if (! pastEnd->run (untilMs))
                            return false;

                        if (pastEnd->getError().isNotEmpty())
                            return end (pastEnd->getError());

                        // What comes next is done by the next step: what is shown says so first.
                        stage = level.isSet() ? Stage::measuring : Stage::writing;
                        return false;

                    case Stage::measuring:
                        chooseGain();
                        stage = Stage::writing;
                        return false;

                    case Stage::writing:
                        break;
                }

                return end (pastEnd->write (export_tail::getLength (getSongEnd (edit).inSeconds(), pastEnd->getLastSoundSeconds()), gainDb));
            }

            /** How far what was rendered is turned up or down: to the loudness wanted, or as the
                other file was; either way its peaks stay under the ceiling. */
            void chooseGain()
            {
                const auto measured = loudness::measureFile (pastEnd->getRendered());

                if (! measured.has_value())
                    return;     // silence is as loud as it gets

                if (level.lufs.has_value())
                {
                    const auto advice = loudness::advise (*measured, *level.lufs);
                    gainDb = advice.gainDb;
                    note = describeLevel (measured->lufs, gainDb, *level.lufs, advice.peakLimited);
                }
                else
                {
                    gainDb = std::min (*level.gainDb, loudness::peakCeilingDb - measured->peakDb);
                }
            }

            void release()
            {
                exact.reset();
                pastEnd.reset();
                temporary.reset();
            }

            bool end (juce::String problem)
            {
                release();
                error = problem;
                finished = true;

                if (error.isEmpty())
                    files.add (file);

                return true;
            }
        };

        //==============================================================================
        /** The song, and then the song without some of its tracks into a second file. */
        class SongWithMinus final : public RoutineJob
        {
        public:
            SongWithMinus (te::Edit& e, const juce::File& f, const SongExport& how)
                : edit (e), file (f), options (how), minusFile (getMinusFile (f, how.without))
            {
                begin (run());
            }

            ~SongWithMinus() override
            {
                stop();

                // Stopped before both were there: neither stays.
                if (! finished)
                    for (const auto& written : files)
                        written.deleteFile();
            }

            juce::String getStage() const override
            {
                const auto* awaited = getAwaited();
                return awaited != nullptr && ! finished ? awaited->getStage() : RoutineJob::getStage();
            }

            int getNumParts() const override            { return 2; }
            int getNumPartsDone() const override        { return files.size(); }

        private:
            te::Edit& edit;
            const juce::File file;
            const SongExport options;
            const juce::File minusFile;

            Steps run()
            {
                auto song = std::make_unique<SongJob> (edit, file, options.format, options.sampleRate, std::nullopt,
                                                       Level { options.loudnessLufs, {} });

                // What cannot be exported says so before any work: the second file too.
                if (song->isFinished())
                {
                    fail (song->getError());
                    co_return;
                }

                if (findHeard (edit, options.without).songTracks.isEmpty())
                {
                    fail ("Nothing to export: without " + describeWithout (options.without) + " no track of the song is heard");
                    co_return;
                }

                say ("Rendering the song", 0, 2);
                co_yield *song;

                if (song->getError().isNotEmpty())
                {
                    fail (song->getError());
                    co_return;
                }

                files.add (file);
                note = song->getNote();

                // As loud as the first: turned by as much, if that was turned at all.
                const Level sameLevel { {}, options.loudnessLufs.has_value() ? std::optional<float> (song->getGainDb()) : std::nullopt };
                song.reset();

                say ("Rendering the song without " + describeWithout (options.without), 1, 2);
                SongJob minus (edit, minusFile, options.format, options.sampleRate, std::nullopt, sameLevel, options.without);
                co_yield minus;

                // The first file is good whatever became of the second.
                if (minus.getError().isNotEmpty())
                    fail (minus.getError());
                else
                    files.add (minusFile);
            }
        };
    }

    //==============================================================================
    std::unique_ptr<ExportJob> ExportJob::song (te::Edit& edit, const juce::File& file, const SongExport& how,
                                                std::optional<te::TimeRange> part)
    {
        // A part of the song is a sample: as it sounds, and alone.
        if (part.has_value())
            return std::make_unique<SongJob> (edit, file, how.format, how.sampleRate, part);

        // A track may have gone since it was chosen.
        auto options = how;
        const auto tracks = te::getAudioTracks (edit);
        options.without.removeIf ([&tracks] (te::AudioTrack* track) { return ! tracks.contains (track); });

        if (options.without.isEmpty())
            return std::make_unique<SongJob> (edit, file, how.format, how.sampleRate, part, Level { how.loudnessLufs, {} });

        return std::make_unique<SongWithMinus> (edit, file, options);
    }

    double getFileSampleRate (const ExportFormat& format, double wanted, double deviceRate)
    {
        const auto rate = wanted > 0.0 ? wanted : deviceRate;

        if (format.type != ExportFormat::Type::mp3 || mp3::canUseSampleRate (rate))
            return rate;

        // Sound at 96 kHz: an MP3 at 48, every other sample of it. Anything else: at 44.1.
        return rate > 48000.0 && std::fmod (rate, 48000.0) == 0.0 ? 48000.0 : 44100.0;
    }

    juce::String getMinusSuffix (const juce::Array<te::AudioTrack*>& without)
    {
        // Only what a file name can have; a track without a name is "minus" too.
        const auto name = without.size() == 1 ? juce::File::createLegalFileName (without.getFirst()->getName()).trim() : juce::String();
        return name.isNotEmpty() ? " (no " + name + ")" : juce::String (" (minus)");
    }

    juce::File getMinusFile (const juce::File& songFile, const juce::Array<te::AudioTrack*>& without)
    {
        return songFile.getSiblingFile (songFile.getFileNameWithoutExtension() + getMinusSuffix (without) + songFile.getFileExtension());
    }

    double getLastExportSampleRate (core::Session& session)
    {
        // Only what the export offers: anything else (another version's setting) is left out.
        const auto rate = session.getEngine().getPropertyStorage().getPropertiesFile().getDoubleValue (sampleRateKey);
        return std::find (exportSampleRates.begin(), exportSampleRates.end(), rate) != exportSampleRates.end() ? rate : 0.0;
    }

    void setLastExportSampleRate (core::Session& session, double rate)
    {
        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        settings.setValue (sampleRateKey, rate);
        settings.saveIfNeeded();
    }

    SongExport getLastSongExport (core::Session& session)
    {
        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        SongExport last (getLastExportFormat (session));
        last.sampleRate = getLastExportSampleRate (session);

        if (settings.containsKey (loudnessKey))
        {
            const auto lufs = settings.getDoubleValue (loudnessKey);

            for (const auto& target : loudnessTargets)
                if (target.lufs == lufs)
                    last.loudnessLufs = lufs;
        }

        return last;
    }

    void setLastSongExport (core::Session& session, const SongExport& how)
    {
        setLastExportFormat (session, how.format);
        setLastExportSampleRate (session, how.sampleRate);

        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();

        if (how.loudnessLufs.has_value())
            settings.setValue (loudnessKey, *how.loudnessLufs);
        else
            settings.removeValue (loudnessKey);

        settings.saveIfNeeded();
    }
}
