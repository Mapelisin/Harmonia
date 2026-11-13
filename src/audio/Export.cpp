#include "audio/Export.h"
#include "audio/ExportJob.h"
#include "audio/ExportRender.h"
#include "audio/Groups.h"

namespace harmonia::audio
{
    using namespace export_render;

    juce::String renderTrack (te::AudioTrack& track, const juce::File& file, te::TimeRange range, bool beforeFader)
    {
        const auto job = ExportJob::track (track, file, range, beforeFader);
        job->finish();
        return job->getError();
    }

    juce::String renderClips (te::AudioTrack& track, const juce::File& file, te::TimeRange range)
    {
        const auto job = ExportJob::clips (track, file, range);
        job->finish();
        return job->getError();
    }

    juce::String exportSong (te::Edit& edit, const juce::File& file, const SongExport& how, std::optional<te::TimeRange> part)
    {
        const auto job = ExportJob::song (edit, file, how, part);
        job->finish();
        return job->getError();
    }

    juce::String exportWav (te::Edit& edit, const juce::File& file, std::optional<te::TimeRange> part)
    {
        return exportSong (edit, file, {}, part);
    }

    juce::String getStemFileName (const juce::String& pattern, const juce::String& songName, int number,
                                  const juce::String& trackName, double bpm, const ExportFormat& format)
    {
        const auto track = trackName.trim().isNotEmpty() ? trackName.trim() : juce::String ("Track");
        const auto song = songName.trim().isNotEmpty() ? songName.trim() : juce::String ("Song");
        const auto roundedBpm = std::round (bpm * 10.0) / 10.0;

        const auto name = (pattern.trim().isNotEmpty() ? pattern.trim() : juce::String (defaultStemPattern))
                              .replace ("{song}", song)
                              .replace ("{n}", juce::String (number).paddedLeft ('0', 2))
                              .replace ("{track}", track)
                              .replace ("{bpm}", std::abs (roundedBpm - std::round (roundedBpm)) < 0.05 ? juce::String (juce::roundToInt (roundedBpm))
                                                                                                        : juce::String (roundedBpm, 1))
                              .replace ("{date}", juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));

        return juce::File::createLegalFileName (name + format.getExtension());
    }

    juce::String getStemFileName (const juce::String& prefix, int number, const juce::String& trackName)
    {
        return getStemFileName (defaultStemPattern, prefix, number, trackName, 120.0);
    }

    juce::Array<te::AudioTrack*> getStemChoices (te::Edit& edit)
    {
        const auto audible = findAudible (edit);
        juce::Array<te::AudioTrack*> choices, groupsHeard;

        for (auto* track : audible.songTracks)
        {
            if (track->getClips().isEmpty())
                continue;

            choices.add (track);

            if (auto* group = groups::getGroup (*track); group != nullptr && audible.groupTracks.contains (group))
                groupsHeard.addIfNotAlreadyThere (group);
        }

        // The groups in the order they are in the song.
        for (auto* track : te::getAudioTracks (edit))
            if (groupsHeard.contains (track))
                choices.add (track);

        return choices;
    }

    StemsResult exportStems (te::Edit& edit, const juce::File& folder, const StemsOptions& options,
                             const std::function<void (int, int)>& progress)
    {
        const auto job = ExportJob::stems (edit, folder, options);
        const auto total = job->getNumParts();
        auto told = -1;

        for (;;)
        {
            // Before each stem.
            if (const auto done = job->getNumPartsDone(); progress != nullptr && ! job->isFinished() && done != told && done < total)
            {
                told = done;
                progress (done, total);
            }

            if (! job->step (1000))
                break;
        }

        if (progress != nullptr && job->getError().isEmpty())
            progress (total, total);

        return { job->getFiles(), job->getError() };
    }

    StemsResult exportStems (te::Edit& edit, const juce::File& folder, const juce::String& prefix,
                             const std::function<void (int, int)>& progress)
    {
        StemsOptions options;
        options.songName = prefix;
        return exportStems (edit, folder, options, progress);
    }

    juce::String getLastStemPattern (core::Session& session)
    {
        const auto pattern = session.getEngine().getPropertyStorage().getPropertiesFile().getValue ("stemNamePattern").trim();
        return pattern.isNotEmpty() ? pattern : juce::String (defaultStemPattern);
    }

    void setLastStemPattern (core::Session& session, const juce::String& pattern)
    {
        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        settings.setValue ("stemNamePattern", pattern.trim());
        settings.saveIfNeeded();
    }
}
