#include "audio/ExportJob.h"
#include "audio/Export.h"
#include "audio/ExportRender.h"
#include "audio/ExportTail.h"
#include "audio/Groups.h"
#include "core/Tracks.h"

namespace harmonia::audio
{
    using namespace export_render;

    bool ExportJob::step (int milliseconds)
    {
        if (finished)
            return false;

        finished = work (juce::Time::getMillisecondCounterHiRes() + juce::jmax (0, milliseconds));
        return ! finished;
    }

    namespace
    {
        //==============================================================================
        class SongJob final : public ExportJob
        {
        public:
            SongJob (te::Edit& e, const juce::File& f, const ExportFormat& wanted, std::optional<te::TimeRange> part)
                : edit (e), file (f), format (wanted)
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
                const auto audible = findAudible (edit);

                if (audible.songTracks.isEmpty())
                {
                    end ("Nothing to export: every track is muted");
                    return;
                }

                auto tracks = audible.others;
                juce::Array<te::AudioTrack*> heard;

                for (const auto* list : { &audible.songTracks, &audible.groupTracks, &audible.returnTracks })
                    heard.addArray (*list);

                for (auto* track : heard)
                    tracks.add (track);

                // A muted kick still ducks the bass, as it does while playing.
                temporary = std::make_unique<TemporaryState> (edit);
                const auto sources = keepSources (*temporary, heard, tracks);

                // A part of the song: exactly that. The song: until it has rung out.
                if (part.has_value())
                    exact = std::make_unique<Render> (edit, file, tracks, *part, true, sources, format);
                else
                    pastEnd = std::make_unique<PastEndRender> (edit, file, tracks, true, sources, format);
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

                return isRendered ? 0.97f : pastEnd != nullptr ? 0.95f * pastEnd->getProgress() : 0.0f;
            }

            juce::String getStage() const override
            {
                if (finished)
                    return error.isEmpty() ? "Done" : "Failed";

                if (isRendered || (exact != nullptr && exact->isEncoding()))
                    return "Writing " + file.getFileName();

                return exact != nullptr ? "Rendering the part of the song" : "Rendering the song";
            }

        private:
            te::Edit& edit;
            const juce::File file;
            const ExportFormat format;
            std::unique_ptr<TemporaryState> temporary;      // put back after the renders are gone
            std::unique_ptr<Render> exact;
            std::unique_ptr<PastEndRender> pastEnd;
            bool isRendered = false;

            bool work (double untilMs) override
            {
                if (exact != nullptr)
                    return exact->run (untilMs) && end (exact->getError());

                if (! isRendered)
                {
                    if (! pastEnd->run (untilMs))
                        return false;

                    if (pastEnd->getError().isNotEmpty())
                        return end (pastEnd->getError());

                    // The file is written by the next step: what is shown says so first.
                    isRendered = true;
                    return false;
                }

                return end (pastEnd->write (export_tail::getLength (getSongEnd (edit).inSeconds(), pastEnd->getLastSoundSeconds())));
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
        class StemsJob final : public ExportJob
        {
        public:
            StemsJob (te::Edit& e, const juce::File& f, const StemsOptions& o)
                : edit (e), folder (f), options (o)
            {
                if (edit.getLength() <= te::TimeDuration())
                {
                    end ("Nothing to export: the song has no clips");
                    return;
                }

                audible = findAudible (edit);
                const auto choices = getStemChoices (edit);

                if (choices.isEmpty())
                {
                    end ("Nothing to export: no track that is heard has clips");
                    return;
                }

                // Those chosen, of what there is; all the song tracks if nothing was chosen.
                for (auto* track : choices)
                    if (options.tracks.isEmpty() ? ! core::tracks::isGroup (*track) : options.tracks.contains (track))
                        stems.add (track);

                if (stems.isEmpty())
                    end ("Nothing to export: none of the tracks chosen is heard and has clips");
            }

            ~StemsJob() override
            {
                release();

                // Stopped while the files were being written: none of them stays.
                if (! finished)
                    for (const auto& written : files)
                        written.deleteFile();
            }

            float getProgress() const override
            {
                if (finished)
                    return 1.0f;

                const auto total = (float) juce::jmax (1, stems.size());
                const auto current = render != nullptr ? render->getProgress() : 0.0f;
                return 0.95f * ((float) numRendered + current) / total + 0.05f * (float) files.size() / total;
            }

            juce::String getStage() const override
            {
                if (finished)
                    return error.isEmpty() ? "Done" : "Failed";

                if (numRendered >= stems.size())
                    return "Writing the files";

                return "Stem " + juce::String (numRendered + 1) + " of " + juce::String (stems.size())
                           + ": " + stems[numRendered]->getName();
            }

            int getNumParts() const override            { return stems.size(); }
            int getNumPartsDone() const override        { return numRendered; }

        private:
            te::Edit& edit;
            const juce::File folder;
            const StemsOptions options;
            Audible audible;
            juce::Array<te::AudioTrack*> stems;
            std::unique_ptr<TemporaryState> temporary;      // of the stem being rendered; put back after its render is gone
            std::unique_ptr<PastEndRender> render;
            std::vector<std::unique_ptr<PastEndRender>> rendered;
            int numRendered = 0;
            double lastSound = 0.0;

            /** The song is set up to play stem `index` alone, and its render made. */
            void begin (int index)
            {
                auto* stem = stems[index];
                const auto isGroup = core::tracks::isGroup (*stem);

                // A song track: the track, its group and the return tracks, only this track playing
                // into them here. A group: the same with every track that plays through it.
                juce::Array<te::AudioTrack*> heard;

                if (isGroup)
                {
                    for (auto* track : audible.songTracks)
                        if (groups::getGroup (*track) == stem)
                            heard.add (track);

                    heard.add (stem);
                }
                else
                {
                    heard.add (stem);

                    if (auto* group = groups::getGroup (*stem))
                        heard.add (group);
                }

                heard.addArray (audible.returnTracks);

                auto tracks = audible.others;

                for (auto* t : heard)
                    tracks.add (t);

                // Numbered as the song has them: its tracks top to bottom, then its groups.
                const auto songTracks = getTracks (edit, false);
                const auto number = isGroup ? songTracks.size() + getTracks (edit, true).indexOf (stem) + 1 : songTracks.indexOf (stem) + 1;
                const auto file = folder.getChildFile (getStemFileName (options.namePattern, options.songName, number, stem->getName(),
                                                                        edit.tempoSequence.getBpmAt (te::TimePosition()), options.format));

                temporary = std::make_unique<TemporaryState> (edit);
                const auto sources = keepSources (*temporary, heard, tracks);

                // The other tracks of its group play into the group whether they are rendered or not:
                // silent for this stem, or it would have them all.
                if (! isGroup)
                    if (auto* group = groups::getGroup (*stem))
                        for (auto* other : audible.songTracks)
                            if (other != stem && groups::getGroup (*other) == group)
                                temporary->keepAsSource (*other);

                render = std::make_unique<PastEndRender> (edit, file, tracks, false, sources, options.format);
            }

            bool work (double untilMs) override
            {
                if (numRendered < stems.size())
                {
                    if (render == nullptr)
                        begin (numRendered);

                    if (! render->run (untilMs))
                        return false;

                    if (render->getError().isNotEmpty())
                        return end (render->getError());

                    temporary.reset();
                    lastSound = std::max (lastSound, render->getLastSoundSeconds());
                    rendered.push_back (std::move (render));
                    ++numRendered;
                    return false;
                }

                // All as long as the one that rings longest; a file a step.
                auto& next = *rendered[(size_t) files.size()];

                if (const auto problem = next.write (export_tail::getLength (getSongEnd (edit).inSeconds(), lastSound)); problem.isNotEmpty())
                    return end (problem);

                files.add (next.getTarget());
                return files.size() == stems.size() && end ({});
            }

            void release()
            {
                render.reset();
                rendered.clear();
                temporary.reset();
            }

            bool end (juce::String problem)
            {
                release();
                error = problem;
                finished = true;
                return true;
            }
        };
    }

    std::unique_ptr<ExportJob> ExportJob::song (te::Edit& edit, const juce::File& file, const ExportFormat& format,
                                                std::optional<te::TimeRange> part)
    {
        return std::make_unique<SongJob> (edit, file, format, part);
    }

    std::unique_ptr<ExportJob> ExportJob::stems (te::Edit& edit, const juce::File& folder, const StemsOptions& options)
    {
        return std::make_unique<StemsJob> (edit, folder, options);
    }
}
