#include "audio/Export.h"
#include "audio/ExportTail.h"
#include "audio/Groups.h"
#include "audio/Mp3Encoder.h"
#include "audio/Sidechain.h"
#include "audio/TrackDelay.h"
#include "core/Clips.h"
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

            /** Its volume and pan left out: the sound as it comes to the fader. */
            void bypassFader (te::AudioTrack& track)
            {
                if (auto* fader = track.getVolumePlugin())
                    set (fader->state, te::IDs::enabled, false);

                // The track's delay is at the fader end too: it goes on working after a freeze.
                for (auto* plugin : track.pluginList)
                    if (dynamic_cast<track_delay::Plugin*> (plugin) != nullptr)
                        set (plugin->state, te::IDs::enabled, false);
            }

            /** Its clips alone: none of its plugins (effects, sends, volume and pan). */
            void bypassPlugins (te::AudioTrack& track)
            {
                for (auto* plugin : track.pluginList)
                    set (plugin->state, te::IDs::enabled, false);
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

        /** Where the song's last clip ends (core::clips knows which clips count). */
        te::TimePosition getSongEnd (te::Edit& edit)
        {
            return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (core::clips::getSongEndBeat (edit)));
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

    namespace
    {
        /** Tracks rendered from the song's start to well past its end, to be cut where their sound
            has died away (audio::export_tail). */
        struct RungOut
        {
            std::unique_ptr<juce::TemporaryFile> wav;       // gone with this
            double lastSoundSeconds = 0.0;
            juce::String error;
        };

        RungOut renderPastEnd (te::Edit& edit, const juce::File& target, const juce::Array<te::Track*>& tracks, bool withMaster,
                               const juce::Array<te::AudioTrack*>& silentSources, const ExportFormat& format)
        {
            RungOut result;

            if (result.error = prepare (target); result.error.isNotEmpty())
                return result;

            // A WAV of the bit depth wanted, its samples then written as they are; for an MP3 a
            // float one, at a rate an MP3 can have.
            ExportFormat rendered { ExportFormat::Type::wav, format.bitDepth };
            auto sampleRate = edit.engine.getDeviceManager().getSampleRate();

            if (format.type == ExportFormat::Type::mp3)
            {
                if (! mp3::isAvailable (format.kbps))
                {
                    result.error = "This computer has no MP3 encoder for " + juce::String (format.kbps) + " kbps: export WAV or FLAC";
                    return result;
                }

                rendered.bitDepth = 32;
                sampleRate = mp3::canUseSampleRate (sampleRate) ? sampleRate : 44100.0;
            }

            const auto songEnd = getSongEnd (edit);
            result.wav = std::make_unique<juce::TemporaryFile> (target.withFileExtension (".wav"));
            result.error = render (edit, result.wav->getFile(), tracks,
                                   { te::TimePosition(), songEnd + te::TimeDuration::fromSeconds (export_tail::longestSeconds) },
                                   withMaster, silentSources, rendered, sampleRate);

            if (result.error.isEmpty())
                result.lastSoundSeconds = export_tail::findLastSound (edit.engine, result.wav->getFile(), songEnd.inSeconds())
                                              .value_or (songEnd.inSeconds());

            return result;
        }

        /** The file of what was rendered, `seconds` long. */
        juce::String writeRungOut (te::Edit& edit, const RungOut& rungOut, const juce::File& target, const ExportFormat& format, double seconds)
        {
            const auto error = export_tail::writeCut (edit.engine, rungOut.wav->getFile(), target, format, seconds);

            // The engine may still hold the WAV it wrote; it goes with the temporary file.
            edit.engine.getAudioFileManager().releaseFile (te::AudioFile (edit.engine, rungOut.wav->getFile()));
            return error;
        }

        /** The song tracks, or the groups, top to bottom. */
        juce::Array<te::AudioTrack*> getTracks (te::Edit& edit, bool groups)
        {
            juce::Array<te::AudioTrack*> found;

            for (auto* track : te::getAudioTracks (edit))
                if (groups ? core::tracks::isGroup (*track) : ! core::tracks::isBus (*track))
                    found.add (track);

            return found;
        }
    }

    juce::String renderTrack (te::AudioTrack& track, const juce::File& file, te::TimeRange range, bool beforeFader)
    {
        TemporaryState temporary (track.edit);
        temporary.hear (track);
        temporary.playToMaster (track);

        if (beforeFader)
            temporary.bypassFader (track);

        auto tracks = getOtherTracks (track.edit);
        tracks.add (&track);

        const auto sources = keepSources (temporary, { &track }, tracks);
        return render (track.edit, file, tracks, range, false, sources);
    }

    juce::String renderClips (te::AudioTrack& track, const juce::File& file, te::TimeRange range)
    {
        TemporaryState temporary (track.edit);
        temporary.hear (track);
        temporary.playToMaster (track);
        temporary.bypassPlugins (track);

        juce::Array<te::Track*> tracks;
        tracks.add (&track);
        return render (track.edit, file, tracks, range, false);
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

        // A part of the song: exactly that.
        if (part.has_value())
            return render (edit, file, tracks, *part, true, sources, format);

        // The song: until it has rung out.
        const auto rungOut = renderPastEnd (edit, file, tracks, true, sources, format);

        if (rungOut.error.isNotEmpty())
            return rungOut.error;

        return writeRungOut (edit, rungOut, file, format, export_tail::getLength (getSongEnd (edit).inSeconds(), rungOut.lastSoundSeconds));
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
        StemsResult result;

        if (edit.getLength() <= te::TimeDuration())
        {
            result.error = "Nothing to export: the song has no clips";
            return result;
        }

        const auto audible = findAudible (edit);
        const auto choices = getStemChoices (edit);

        if (choices.isEmpty())
        {
            result.error = "Nothing to export: no track that is heard has clips";
            return result;
        }

        // Those chosen, of what there is; all the song tracks if nothing was chosen.
        juce::Array<te::AudioTrack*> stems;

        for (auto* track : choices)
            if (options.tracks.isEmpty() ? ! core::tracks::isGroup (*track) : options.tracks.contains (track))
                stems.add (track);

        if (stems.isEmpty())
        {
            result.error = "Nothing to export: none of the tracks chosen is heard and has clips";
            return result;
        }

        const auto songTracks = getTracks (edit, false);
        const auto groupTracks = getTracks (edit, true);
        const auto bpm = edit.tempoSequence.getBpmAt (te::TimePosition());

        struct Stem
        {
            juce::File file;
            RungOut rungOut;
        };

        std::vector<Stem> rendered;
        auto lastSound = 0.0;

        for (int i = 0; i < stems.size(); ++i)
        {
            if (progress != nullptr)
                progress (i, stems.size());

            auto* stem = stems[i];
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
            const auto number = isGroup ? songTracks.size() + groupTracks.indexOf (stem) + 1 : songTracks.indexOf (stem) + 1;
            const auto file = folder.getChildFile (getStemFileName (options.namePattern, options.songName, number, stem->getName(), bpm, options.format));

            TemporaryState temporary (edit);
            const auto sources = keepSources (temporary, heard, tracks);

            // The other tracks of its group play into the group whether they are rendered or not:
            // silent for this stem, or it would have them all.
            if (! isGroup)
                if (auto* group = groups::getGroup (*stem))
                    for (auto* other : audible.songTracks)
                        if (other != stem && groups::getGroup (*other) == group)
                            temporary.keepAsSource (*other);

            auto rungOut = renderPastEnd (edit, file, tracks, false, sources, options.format);

            if (rungOut.error.isNotEmpty())
            {
                result.error = rungOut.error;
                return result;
            }

            lastSound = std::max (lastSound, rungOut.lastSoundSeconds);
            rendered.push_back ({ file, std::move (rungOut) });
        }

        // All as long as the one that rings longest.
        const auto seconds = export_tail::getLength (getSongEnd (edit).inSeconds(), lastSound);

        for (const auto& stem : rendered)
        {
            if (const auto error = writeRungOut (edit, stem.rungOut, stem.file, options.format, seconds); error.isNotEmpty())
            {
                result.error = error;
                return result;
            }

            result.files.add (stem.file);
        }

        if (progress != nullptr)
            progress (stems.size(), stems.size());

        return result;
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
