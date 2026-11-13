#include "audio/ExportRender.h"
#include "audio/ExportTail.h"
#include "audio/Groups.h"
#include "audio/Mp3Encoder.h"
#include "audio/Sidechain.h"
#include "audio/TrackDelay.h"
#include "core/Clips.h"
#include "core/Tracks.h"

namespace harmonia::audio::export_render
{
    namespace
    {
        juce::String prepare (const juce::File& file)
        {
            if (! file.getParentDirectory().createDirectory())
                return "Cannot create folder: " + file.getParentDirectory().getFullPathName();

            if (file.existsAsFile() && ! file.deleteFile())
                return "Cannot overwrite " + file.getFullPathName() + " (is it open in another program?)";

            return {};
        }

        juce::String noMp3Encoder (int kbps)
        {
            return "This computer has no MP3 encoder for " + juce::String (kbps) + " kbps: export WAV or FLAC";
        }
    }

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

    juce::Array<te::Track*> getOtherTracks (te::Edit& edit)
    {
        juce::Array<te::Track*> others;

        for (auto* track : te::getAllTracks (edit))
            if (dynamic_cast<te::AudioTrack*> (track) == nullptr)
                others.add (track);

        return others;
    }

    juce::Array<te::AudioTrack*> getTracks (te::Edit& edit, bool groups)
    {
        juce::Array<te::AudioTrack*> found;

        for (auto* track : te::getAudioTracks (edit))
            if (groups ? core::tracks::isGroup (*track) : ! core::tracks::isBus (*track))
                found.add (track);

        return found;
    }

    te::TimePosition getSongEnd (te::Edit& edit)
    {
        return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (core::clips::getSongEndBeat (edit)));
    }

    //==============================================================================
    TemporaryState::TemporaryState (te::Edit& e)
        : edit (e), wasChanged (e.hasChangedSinceSaved())
    {
    }

    TemporaryState::~TemporaryState()
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

    void TemporaryState::hear (te::AudioTrack& track)
    {
        set (track.state, te::IDs::mute, false);
        set (track.state, te::IDs::soloIsolate, true);
    }

    void TemporaryState::playToMaster (te::AudioTrack& track)
    {
        if (groups::getGroup (track) == nullptr)
            return;

        // The name, which the output follows, before the group's ID kept next to it: put
        // back in the reverse order, the ID is there when the name looks for it.
        auto output = track.state.getChildWithName (te::IDs::OUTPUTDEVICES).getChildWithName (te::IDs::DEVICE);
        set (output, te::IDs::name, te::DeviceManager::getDefaultAudioOutDeviceName (false));
        set (output, "destTrackID", {});
    }

    void TemporaryState::bypassFader (te::AudioTrack& track)
    {
        if (auto* fader = track.getVolumePlugin())
            set (fader->state, te::IDs::enabled, false);

        // The track's delay is at the fader end too: it goes on working after a freeze.
        for (auto* plugin : track.pluginList)
            if (dynamic_cast<track_delay::Plugin*> (plugin) != nullptr)
                set (plugin->state, te::IDs::enabled, false);
    }

    void TemporaryState::bypassPlugins (te::AudioTrack& track)
    {
        for (auto* plugin : track.pluginList)
            set (plugin->state, te::IDs::enabled, false);
    }

    void TemporaryState::keepAsSource (te::AudioTrack& track)
    {
        set (track.state, te::IDs::mute, true);
    }

    void TemporaryState::set (juce::ValueTree tree, const juce::Identifier& id, const juce::var& value)
    {
        saved.emplace_back (tree, id, tree.getPropertyPointer (id) != nullptr ? std::optional<juce::var> (tree[id]) : std::nullopt);

        if (value.isVoid())
            tree.removeProperty (id, nullptr);
        else
            tree.setProperty (id, value, nullptr);
    }

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

    //==============================================================================
    Render::Render (te::Edit& e, const juce::File& f, const juce::Array<te::Track*>& tracks, te::TimeRange range, bool withMaster,
                    const juce::Array<te::AudioTrack*>& silentSources, const ExportFormat& wanted, double sampleRate)
        : edit (e), file (f), format (wanted), rendered (f)
    {
        if (const auto problem = prepare (file); problem.isNotEmpty())
        {
            finish (problem);
            return;
        }

        if (sampleRate <= 0)
            sampleRate = edit.engine.getDeviceManager().getSampleRate();

        // An MP3 is made from a float WAV rendered next to it, at a rate an MP3 can have.
        auto written = format;

        if (format.type == ExportFormat::Type::mp3)
        {
            if (! mp3::isAvailable (format.kbps))
            {
                finish (noMp3Encoder (format.kbps));
                return;
            }

            wav = std::make_unique<juce::TemporaryFile> (file.withFileExtension (".wav"));
            rendered = wav->getFile();
            written = { ExportFormat::Type::wav, 32 };
            sampleRate = mp3::canUseSampleRate (sampleRate) ? sampleRate : 44100.0;
        }

        renderStatus.emplace (edit, true);
        te::TransportControl::stopAllTransports (edit.engine, false, true);

        te::Renderer::Parameters parameters (edit);
        parameters.destFile = rendered;
        auto& formats = edit.engine.getAudioFileFormatManager();
        parameters.audioFormat = written.type == ExportFormat::Type::flac ? formats.getFlacFormat() : formats.getWavFormat();
        parameters.bitDepth = written.bitDepth;
        parameters.ditheringEnabled = written.bitDepth == 16;
        parameters.sampleRateForAudio = sampleRate;
        parameters.blockSizeForAudio = edit.engine.getDeviceManager().getBlockSize();
        parameters.time = range;
        parameters.usePlugins = true;
        parameters.useMasterPlugins = withMaster;
        parameters.tracksToDo = te::toBitSet (tracks);

        for (auto* source : silentSources)
            parameters.tracksToProcessWhileMuted.add (source->itemID);

        task = te::render_utils::createRenderTask (parameters, "Export", &progress, nullptr);

        if (task == nullptr)
            finish ("Export failed: " + file.getFullPathName());
    }

    Render::~Render()
    {
        if (stage == Stage::done)
            return;

        // Stopped half way: nothing is left of it.
        task.reset();
        renderStatus.reset();
        release (rendered);
        rendered.deleteFile();
    }

    void Render::release (const juce::File& held)
    {
        // The engine may still hold a file it wrote.
        edit.engine.getAudioFileManager().releaseFile (te::AudioFile (edit.engine, held));
    }

    void Render::finish (const juce::String& problem)
    {
        task.reset();
        renderStatus.reset();
        error = problem;
        stage = Stage::done;
    }

    bool Render::run (double untilMs)
    {
        if (stage == Stage::done)
            return true;

        if (stage == Stage::encoding)
        {
            const auto problem = mp3::encode (rendered, file, format.kbps);
            release (rendered);         // it goes with the temporary file
            finish (problem);
            return true;
        }

        auto status = juce::ThreadPoolJob::jobNeedsRunningAgain;

        do
        {
            status = task->runJob();
        }
        while (status == juce::ThreadPoolJob::jobNeedsRunningAgain && juce::Time::getMillisecondCounterHiRes() < untilMs);

        if (status == juce::ThreadPoolJob::jobNeedsRunningAgain)
            return false;

        const auto taskError = task->errorMessage;
        task.reset();
        renderStatus.reset();

        if (taskError.isNotEmpty() || ! rendered.existsAsFile())
        {
            rendered.deleteFile();
            finish ("Export failed: " + file.getFullPathName() + (taskError.isNotEmpty() ? " (" + taskError + ")" : juce::String()));
            return true;
        }

        // The MP3 is made by the next run, so that whoever shows the progress can say so first.
        if (wav != nullptr)
        {
            stage = Stage::encoding;
            return false;
        }

        stage = Stage::done;
        return true;
    }

    float Render::getProgress() const noexcept
    {
        if (stage == Stage::done)
            return 1.0f;

        const auto renderedShare = stage == Stage::encoding ? 1.0f : juce::jlimit (0.0f, 1.0f, progress.load());
        return wav != nullptr ? renderedShare * 0.9f : renderedShare;
    }

    //==============================================================================
    PastEndRender::PastEndRender (te::Edit& e, const juce::File& t, const juce::Array<te::Track*>& tracks, bool withMaster,
                                  const juce::Array<te::AudioTrack*>& silentSources, const ExportFormat& wanted)
        : edit (e), target (t), format (wanted)
    {
        if (error = prepare (target); error.isNotEmpty())
        {
            finished = true;
            return;
        }

        // A WAV of the bit depth wanted, its samples then written as they are; for an MP3 a
        // float one, at a rate an MP3 can have.
        ExportFormat rendered { ExportFormat::Type::wav, format.bitDepth };
        auto sampleRate = edit.engine.getDeviceManager().getSampleRate();

        if (format.type == ExportFormat::Type::mp3)
        {
            if (! mp3::isAvailable (format.kbps))
            {
                error = noMp3Encoder (format.kbps);
                finished = true;
                return;
            }

            rendered.bitDepth = 32;
            sampleRate = mp3::canUseSampleRate (sampleRate) ? sampleRate : 44100.0;
        }

        const auto songEnd = getSongEnd (edit);
        songEndSeconds = songEnd.inSeconds();
        wav = std::make_unique<juce::TemporaryFile> (target.withFileExtension (".wav"));
        render = std::make_unique<Render> (edit, wav->getFile(), tracks,
                                           te::TimeRange (te::TimePosition(), songEnd + te::TimeDuration::fromSeconds (export_tail::longestSeconds)),
                                           withMaster, silentSources, rendered, sampleRate);
    }

    PastEndRender::~PastEndRender()
    {
        render.reset();

        // The engine may still hold the WAV it wrote; it goes with the temporary file.
        if (wav != nullptr)
            edit.engine.getAudioFileManager().releaseFile (te::AudioFile (edit.engine, wav->getFile()));
    }

    bool PastEndRender::run (double untilMs)
    {
        if (finished)
            return true;

        if (! render->run (untilMs))
            return false;

        error = render->getError();
        render.reset();

        if (error.isEmpty())
            lastSoundSeconds = export_tail::findLastSound (edit.engine, wav->getFile(), songEndSeconds).value_or (songEndSeconds);

        finished = true;
        return true;
    }

    float PastEndRender::getProgress() const noexcept
    {
        return finished ? 1.0f : render != nullptr ? render->getProgress() : 0.0f;
    }

    juce::String PastEndRender::write (double seconds)
    {
        if (! finished || error.isNotEmpty() || wav == nullptr)
            return error.isNotEmpty() ? error : juce::String ("Export failed: " + target.getFullPathName());

        return export_tail::writeCut (edit.engine, wav->getFile(), target, format, seconds);
    }
}
