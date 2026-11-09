#include "audio/Export.h"
#include "audio/Groups.h"
#include "audio/Mp3Encoder.h"
#include "audio/Sidechain.h"
#include "core/Tracks.h"

namespace harmonia::audio
{
    namespace
    {
        /** The tracks exportWav plays: song, group and return tracks as they're heard (muted ones
            left out; while some are soloed, only those and the solo-safe ones), and the edit's other
            tracks (tempo, master...). */
        struct Audible
        {
            juce::Array<te::Track*> others;
            juce::Array<te::AudioTrack*> songTracks, groupTracks, returnTracks;
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

                if (core::tracks::isReturn (*audioTrack))
                    result.returnTracks.add (audioTrack);
                else if (core::tracks::isGroup (*audioTrack))
                    result.groupTracks.add (audioTrack);
                else
                    result.songTracks.add (audioTrack);
            }

            // A soloed track is heard through its group, though the group itself isn't soloed.
            for (auto* track : result.songTracks)
                if (auto* group = groups::getGroup (*track))
                    result.groupTracks.addIfNotAlreadyThere (group);

            return result;
        }

        /** The edit's tracks that aren't audio tracks (tempo, master...): every render has them. */
        juce::Array<te::Track*> getOtherTracks (te::Edit& edit)
        {
            juce::Array<te::Track*> others;

            for (auto* track : te::getAllTracks (edit))
                if (dynamic_cast<te::AudioTrack*> (track) == nullptr)
                    others.add (track);

            return others;
        }

        /** Property changes made straight in the edit's state for one render and put back after
            it: not undo steps, nor changes to save. */
        class TemporaryState
        {
        public:
            explicit TemporaryState (te::Edit& e) : edit (e), wasChanged (e.hasChangedSinceSaved()) {}

            ~TemporaryState()
            {
                for (auto it = saved.rbegin(); it != saved.rend(); ++it)
                {
                    auto& [tree, id, value] = *it;

                    if (value.has_value())
                        tree.setProperty (id, *value, nullptr);
                    else
                        tree.removeProperty (id, nullptr);
                }

                // Tracktion counts any mute or solo change as a change to save.
                if (! wasChanged)
                    edit.resetChangedStatus();
            }

            /** Heard whatever its mute and the others' solo say. */
            void hear (te::AudioTrack& track)
            {
                set (track.state, te::IDs::mute, false);
                set (track.state, te::IDs::soloIsolate, true);
            }

            /** Played straight out rather than into its group (which this render doesn't have). */
            void playToMaster (te::AudioTrack& track)
            {
                if (groups::getGroup (track) == nullptr)
                    return;

                // The name, which the output follows, before the group's ID kept next to it: put
                // back in the reverse order, the ID is there when the name looks for it.
                auto output = track.state.getChildWithName (te::IDs::OUTPUTDEVICES).getChildWithName (te::IDs::DEVICE);
                set (output, te::IDs::name, te::DeviceManager::getDefaultAudioOutDeviceName (false));
                set (output, "destTrackID", {});
            }

            /** Silent, though still feeding the effects that listen to it: Tracktion keeps a
                muted track playing for them, without its sends to the return tracks. */
            void keepAsSource (te::AudioTrack& track)
            {
                set (track.state, te::IDs::mute, true);
            }

        private:
            te::Edit& edit;
            bool wasChanged;
            std::vector<std::tuple<juce::ValueTree, juce::Identifier, std::optional<juce::var>>> saved;

            /** A void value removes the property. */
            void set (juce::ValueTree tree, const juce::Identifier& id, const juce::var& value)
            {
                saved.emplace_back (tree, id, tree.getPropertyPointer (id) != nullptr ? std::optional<juce::var> (tree[id]) : std::nullopt);

                if (value.isVoid())
                    tree.removeProperty (id, nullptr);
                else
                    tree.setProperty (id, value, nullptr);
            }
        };

        /** The tracks the effects of `heard` listen to (sidechain), added to `tracks` silent:
            a part of the song rendered alone sounds as it does in the whole song. */
        juce::Array<te::AudioTrack*> keepSources (TemporaryState& temporary, const juce::Array<te::AudioTrack*>& heard,
                                                  juce::Array<te::Track*>& tracks)
        {
            auto sources = sidechain::getSourcesFor (heard);

            for (auto* source : sources)
            {
                temporary.keepAsSource (*source);
                tracks.add (source);
            }

            return sources;
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
                             te::TimeRange range, bool withMaster, const juce::Array<te::AudioTrack*>& silentSources = {},
                             const ExportFormat& format = {}, double sampleRate = 0)
        {
            if (const auto error = prepare (file); error.isNotEmpty())
                return error;

            if (sampleRate <= 0)
                sampleRate = edit.engine.getDeviceManager().getSampleRate();

            // An MP3 is made from a float WAV rendered next to it.
            if (format.type == ExportFormat::Type::mp3)
            {
                if (! mp3::isAvailable (format.kbps))
                    return "This computer has no MP3 encoder for " + juce::String (format.kbps) + " kbps: export WAV or FLAC";

                const juce::TemporaryFile wav (file.withFileExtension (".wav"));
                const auto error = render (edit, wav.getFile(), tracks, range, withMaster, silentSources,
                                           { ExportFormat::Type::wav, 32 },
                                           mp3::canUseSampleRate (sampleRate) ? sampleRate : 44100.0);

                if (error.isNotEmpty())
                    return error;

                const auto encoded = mp3::encode (wav.getFile(), file, format.kbps);

                // The engine may still hold the WAV it wrote; it goes with the temporary file.
                edit.engine.getAudioFileManager().releaseFile (te::AudioFile (edit.engine, wav.getFile()));
                return encoded;
            }

            const te::Edit::ScopedRenderStatus renderStatus (edit, true);
            te::TransportControl::stopAllTransports (edit.engine, false, true);

            te::Renderer::Parameters parameters (edit);
            parameters.destFile = file;
            auto& formats = edit.engine.getAudioFileFormatManager();
            parameters.audioFormat = format.type == ExportFormat::Type::flac ? formats.getFlacFormat() : formats.getWavFormat();
            parameters.bitDepth = format.bitDepth;
            parameters.ditheringEnabled = format.bitDepth == 16;
            parameters.sampleRateForAudio = sampleRate;
            parameters.blockSizeForAudio = edit.engine.getDeviceManager().getBlockSize();
            parameters.time = range;
            parameters.usePlugins = true;
            parameters.useMasterPlugins = withMaster;
            parameters.tracksToDo = te::toBitSet (tracks);

            for (auto* source : silentSources)
                parameters.tracksToProcessWhileMuted.add (source->itemID);

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
        TemporaryState temporary (track.edit);
        temporary.hear (track);
        temporary.playToMaster (track);

        auto tracks = getOtherTracks (track.edit);
        tracks.add (&track);

        const auto sources = keepSources (temporary, { &track }, tracks);
        return render (track.edit, file, tracks, range, false, sources);
    }

    juce::String exportSong (te::Edit& edit, const juce::File& file, const ExportFormat& format, std::optional<te::TimeRange> part)
    {
        if (edit.getLength() <= te::TimeDuration())
            return "Nothing to export: the song has no clips";

        if (part.has_value() && (part->getLength() <= te::TimeDuration() || part->getStart() < te::TimePosition()))
            return "Nothing to export: the part to export is empty";

        // What you hear is what you get.
        const auto audible = findAudible (edit);

        if (audible.songTracks.isEmpty())
            return "Nothing to export: every track is muted";

        auto tracks = audible.others;
        juce::Array<te::AudioTrack*> heard;

        for (const auto* list : { &audible.songTracks, &audible.groupTracks, &audible.returnTracks })
            heard.addArray (*list);

        for (auto* track : heard)
            tracks.add (track);

        // A muted kick still ducks the bass, as it does while playing.
        TemporaryState temporary (edit);
        const auto sources = keepSources (temporary, heard, tracks);
        return render (edit, file, tracks, part.value_or (getRange (edit)), true, sources, format);
    }

    juce::String exportWav (te::Edit& edit, const juce::File& file, std::optional<te::TimeRange> part)
    {
        return exportSong (edit, file, {}, part);
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
                if (! core::tracks::isBus (*track))
                    all.add (track);

            return all;
        }();

        for (int i = 0; i < withClips.size(); ++i)
        {
            if (progress != nullptr)
                progress (i, withClips.size());

            auto* track = withClips[i];

            // The track, its group and the return tracks: only this track plays into them here.
            juce::Array<te::AudioTrack*> heard { track };

            if (auto* group = groups::getGroup (*track))
                heard.add (group);

            heard.addArray (audible.returnTracks);

            auto tracks = audible.others;

            for (auto* t : heard)
                tracks.add (t);

            TemporaryState temporary (edit);
            const auto sources = keepSources (temporary, heard, tracks);
            const auto file = folder.getChildFile (getStemFileName (prefix, allSongTracks.indexOf (track) + 1, track->getName()));

            if (const auto error = render (edit, file, tracks, range, false, sources); error.isNotEmpty())
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
