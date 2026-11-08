#include "audio/Export.h"
#include "core/Tracks.h"

namespace harmonia::audio
{
    namespace
    {
        /** The tracks exportWav plays: song and return tracks as they're heard (muted ones left out;
            while some are soloed, only those and the solo-safe ones), and the edit's other tracks
            (tempo, master...). */
        struct Audible
        {
            juce::Array<te::Track*> others;
            juce::Array<te::AudioTrack*> songTracks, returnTracks;
        };

        Audible findAudible (te::Edit& edit)
        {
            Audible result;
            const auto anySolo = edit.areAnyTracksSolo();

            for (auto* track : te::getAllTracks (edit))
            {
                auto* audioTrack = dynamic_cast<te::AudioTrack*> (track);

                if (audioTrack == nullptr)
                {
                    result.others.add (track);
                    continue;
                }

                if (track->isMuted (true) || (anySolo && ! (track->isSolo (true) || track->isSoloIsolate (true))))
                    continue;

                (core::tracks::isReturn (*audioTrack) ? result.returnTracks : result.songTracks).add (audioTrack);
            }

            return result;
        }

        te::TimeRange getRange (te::Edit& edit)
        {
            return { te::TimePosition(), te::TimePosition() + edit.getLength() + te::TimeDuration::fromSeconds (exportTailSeconds) };
        }

        juce::String prepare (const juce::File& file)
        {
            if (! file.getParentDirectory().createDirectory())
                return "Cannot create folder: " + file.getParentDirectory().getFullPathName();

            if (file.existsAsFile() && ! file.deleteFile())
                return "Cannot overwrite " + file.getFullPathName() + " (is it open in another program?)";

            return {};
        }

        /** Renders these tracks right here (no progress window). Unlike Renderer::renderToFile,
            changes nothing in the edit: that one solo-isolates and unmutes the rendered tracks for
            the time being, which Tracktion counts as a change to save (the project showed unsaved
            changes after an export). The tracks given here are all heard anyway. */
        juce::String render (te::Edit& edit, const juce::File& file, const juce::Array<te::Track*>& tracks,
                             te::TimeRange range, bool withMaster)
        {
            if (const auto error = prepare (file); error.isNotEmpty())
                return error;

            const te::Edit::ScopedRenderStatus renderStatus (edit, true);
            te::TransportControl::stopAllTransports (edit.engine, false, true);

            te::Renderer::Parameters parameters (edit);
            parameters.destFile = file;
            parameters.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
            parameters.bitDepth = 24;
            parameters.sampleRateForAudio = edit.engine.getDeviceManager().getSampleRate();
            parameters.blockSizeForAudio = edit.engine.getDeviceManager().getBlockSize();
            parameters.time = range;
            parameters.usePlugins = true;
            parameters.useMasterPlugins = withMaster;
            parameters.tracksToDo = te::toBitSet (tracks);

            juce::String taskError;

            if (auto task = te::render_utils::createRenderTask (parameters, "Export", nullptr, nullptr))
            {
                while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
                {}

                taskError = task->errorMessage;
            }

            if (taskError.isNotEmpty() || ! file.existsAsFile())
            {
                file.deleteFile();
                return "Export failed: " + file.getFullPathName() + (taskError.isNotEmpty() ? " (" + taskError + ")" : juce::String());
            }

            return {};
        }
    }

    juce::String renderTrack (te::AudioTrack& track, const juce::File& file, te::TimeRange range)
    {
        // Heard for the render whatever its mute and the others' solo say. Put straight into the
        // state and back, not as undo steps.
        struct Heard
        {
            explicit Heard (te::AudioTrack& t) : edit (t.edit), state (t.state), wasChanged (t.edit.hasChangedSinceSaved())
            {
                for (const auto& [id, value] : { std::pair { te::IDs::mute, false }, std::pair { te::IDs::soloIsolate, true } })
                {
                    saved.push_back ({ id, state.getPropertyPointer (id) != nullptr ? std::optional<juce::var> (state[id]) : std::nullopt });
                    state.setProperty (id, value, nullptr);
                }
            }

            ~Heard()
            {
                for (const auto& [id, value] : saved)
                {
                    if (value.has_value())
                        state.setProperty (id, *value, nullptr);
                    else
                        state.removeProperty (id, nullptr);
                }

                // Tracktion counts any mute or solo change as a change to save.
                if (! wasChanged)
                    edit.resetChangedStatus();
            }

            te::Edit& edit;
            juce::ValueTree state;
            bool wasChanged;
            std::vector<std::pair<juce::Identifier, std::optional<juce::var>>> saved;
        };

        juce::Array<te::Track*> tracks;

        for (auto* other : te::getAllTracks (track.edit))
            if (dynamic_cast<te::AudioTrack*> (other) == nullptr)
                tracks.add (other);

        tracks.add (&track);

        const Heard heard (track);
        return render (track.edit, file, tracks, range, false);
    }

    juce::String exportWav (te::Edit& edit, const juce::File& file)
    {
        if (edit.getLength() <= te::TimeDuration())
            return "Nothing to export: the song has no clips";

        // What you hear is what you get.
        const auto audible = findAudible (edit);

        if (audible.songTracks.isEmpty())
            return "Nothing to export: every track is muted";

        auto tracks = audible.others;

        for (auto* track : audible.songTracks)
            tracks.add (track);

        for (auto* track : audible.returnTracks)
            tracks.add (track);

        return render (edit, file, tracks, getRange (edit), true);
    }

    juce::String getStemFileName (const juce::String& prefix, int number, const juce::String& trackName)
    {
        const auto name = trackName.trim().isNotEmpty() ? trackName.trim() : juce::String ("Track");
        return juce::File::createLegalFileName (prefix + " - " + juce::String (number).paddedLeft ('0', 2) + " " + name + ".wav");
    }

    StemsResult exportStems (te::Edit& edit, const juce::File& folder, const juce::String& prefix,
                             const std::function<void (int, int)>& progress)
    {
        StemsResult result;

        if (edit.getLength() <= te::TimeDuration())
        {
            result.error = "Nothing to export: the song has no clips";
            return result;
        }

        const auto audible = findAudible (edit);
        juce::Array<te::AudioTrack*> withClips;

        for (auto* track : audible.songTracks)
            if (! track->getClips().isEmpty())
                withClips.add (track);

        if (withClips.isEmpty())
        {
            result.error = "Nothing to export: no track that is heard has clips";
            return result;
        }

        const auto range = getRange (edit);
        const auto allSongTracks = [&]
        {
            juce::Array<te::AudioTrack*> all;

            for (auto* track : te::getAudioTracks (edit))
                if (! core::tracks::isReturn (*track))
                    all.add (track);

            return all;
        }();

        for (int i = 0; i < withClips.size(); ++i)
        {
            if (progress != nullptr)
                progress (i, withClips.size());

            auto* track = withClips[i];

            // The track and the return tracks: only this track sends to them here.
            auto tracks = audible.others;
            tracks.add (track);

            for (auto* returnTrack : audible.returnTracks)
                tracks.add (returnTrack);

            const auto file = folder.getChildFile (getStemFileName (prefix, allSongTracks.indexOf (track) + 1, track->getName()));

            if (const auto error = render (edit, file, tracks, range, false); error.isNotEmpty())
            {
                result.error = error;
                return result;
            }

            result.files.add (file);
        }

        if (progress != nullptr)
            progress (withClips.size(), withClips.size());

        return result;
    }
}
