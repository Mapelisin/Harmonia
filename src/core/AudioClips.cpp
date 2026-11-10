#include "core/AudioClips.h"
#include "core/AudioTempo.h"

namespace harmonia::core::audio_clips
{
    namespace
    {
        const juce::Identifier crossfadesOffId ("hNoCrossfades");      // on an audio clip's node

        /** FLAC, Ogg, MP3...: files the engine can't read straight from the disk while it plays. */
        bool isCompressed (const te::AudioFile& file)
        {
            return ! file.isNull() && file.getInfo().needsCachedProxy;
        }

        /** Decodes `original` into a WAV at `target` (as the engine's own background job would). */
        bool decode (te::Engine& engine, const te::AudioFile& original, const juce::File& target)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (te::AudioFileUtils::createReaderFor (engine, original.getFile()));

            if (reader == nullptr || ! target.getParentDirectory().createDirectory())
                return false;

            auto info = original.getInfo();
            auto* wav = engine.getAudioFileFormatManager().getWavFormat();

            if (! wav->isChannelLayoutSupported (info.channelLayout))
                info.channelLayout = juce::AudioChannelSet::discreteChannels (info.channelLayout.size());

            juce::TemporaryFile temp (target);
            auto ok = false;

            {
                te::AudioFileWriter writer (te::AudioFile (engine, temp.getFile()), wav, info.numChannels, info.sampleRate,
                                            std::max (16, info.bitsPerSample), info.metadata, 0, info.channelLayout);
                ok = writer.isOpen() && writer.writeFromAudioReader (*reader, 0, reader->lengthInSamples);
            }

            if (! ok || ! temp.overwriteTargetFileWithTemporary())
                return false;

            engine.getAudioFileManager().releaseFile (te::AudioFile (engine, target));
            return true;
        }

        /** The WAV copy the engine plays a compressed file from, made now if it isn't there. */
        void makeDecodedCopy (te::AudioClipBase& clip)
        {
            const auto original = clip.getAudioFile();

            if (! clip.canUseProxy() || ! isCompressed (original))
                return;

            if (const auto copy = clip.getPlaybackFile(); ! copy.isNull() && copy != original && ! copy.getFile().existsAsFile())
                decode (clip.edit.engine, original, copy.getFile());
        }

        /** Stretched while it plays, the file is read straight from the disk: a compressed one is
            replaced by a WAV of it, in the folder the song's new sound files go to. */
        void useDecodedSource (te::AudioClipBase& clip)
        {
            const auto original = clip.getAudioFile();
            auto* track = clip.getTrack();

            if (! isCompressed (original) || track == nullptr)
                return;

            auto& engine = clip.edit.engine;
            const auto folder = engine.getEngineBehaviour().getFileForNewAudioRecording (*track, ".wav").getParentDirectory();
            const auto target = folder.getNonexistentChildFile (original.getFile().getFileNameWithoutExtension(), ".wav", false);

            if (decode (engine, original, target))
                clip.getSourceFileReference().setToFile (target, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
        }
    }

    void prepareForPlayback (te::AudioClipBase& clip)
    {
        // Setting a value a clip already has changes nothing (and adds nothing to undo).
        clip.setAutoPitch (false);

        // Crossfades with the clips it overlaps, unless turned off for it.
        if (! clip.getAutoCrossfade() && ! clip.state.hasProperty (crossfadesOffId))
        {
            clip.setAutoCrossfade (true);
            clip.updateAutoCrossfadesAsync (true);
        }

        if (clip.getAutoTempo() || std::abs (clip.getPitchChange()) > 0.001f)
        {
            clip.setUsesProxy (false);
            useDecodedSource (clip);

            if (! te::TimeStretcher::canProcessFor (clip.getTimeStretchMode()))
                clip.setTimeStretchMode (te::TimeStretcher::defaultMode);
        }
        else
        {
            clip.setUsesProxy (true);
            makeDecodedCopy (clip);
        }
    }

    bool isLooping (const te::AudioClipBase& clip)
    {
        return clip.isLooping();
    }

    void setLooping (te::AudioClipBase& clip, bool shouldLoop)
    {
        if (shouldLoop == clip.isLooping())
            return;

        if (! shouldLoop)
        {
            clip.disableLooping();
            prepareForPlayback (clip);
            return;
        }

        const auto position = clip.getPosition();
        const auto offset = position.getOffset();

        if (clip.getAutoTempo())
        {
            // Beats of the loop, counted at the tempo where the clip starts (as the engine does).
            const auto beatsPerSecond = clip.edit.tempoSequence.getBeatsPerSecondAt (position.getStart());
            const auto start = te::BeatPosition::fromBeats (offset.inSeconds() * beatsPerSecond);
            clip.setLoopRangeBeats ({ start, start + (clip.getEndBeat() - clip.getStartBeat()) });
        }
        else
        {
            const auto length = std::min (clip.getSourceLength() / clip.getSpeedRatio() - offset, position.getLength());

            if (length <= te::TimeDuration())
                return;

            clip.setLoopRange ({ te::toPosition (offset), te::toPosition (offset) + length });
        }

        // The clip starts at the loop's start.
        clip.setOffset ({});
        prepareForPlayback (clip);
    }

    double getLoopLengthBeats (const te::AudioClipBase& clip)
    {
        if (! clip.isLooping())
            return 0.0;

        if (clip.beatBasedLooping())
            return clip.getLoopLengthBeats().inBeats();

        const auto& tempo = clip.edit.tempoSequence;
        const auto start = clip.getPosition().getStart();
        return (tempo.toBeats (start + clip.getLoopLength()) - tempo.toBeats (start)).inBeats();
    }

    double getLoopSongStartBeat (const te::AudioClipBase& clip)
    {
        const auto& tempo = clip.edit.tempoSequence;
        const auto position = clip.getPosition();

        // A tempo-following clip's offset is counted in beats at the tempo where it starts.
        if (clip.beatBasedLooping())
            return tempo.toBeats (position.getStart()).inBeats()
                 - position.getOffset().inSeconds() * tempo.getBeatsPerSecondAt (position.getStart());

        return tempo.toBeats (position.getStart() - position.getOffset()).inBeats();
    }

    juce::Range<double> getLoopFileSeconds (const te::AudioClipBase& clip)
    {
        if (! clip.isLooping())
            return {};

        if (clip.beatBasedLooping())
        {
            const auto map = audio_tempo::getBeatMap (clip);

            if (! map.has_value())
                return {};

            const auto start = clip.getLoopStartBeats().inBeats();
            return { map->toSeconds (start), map->toSeconds (start + clip.getLoopLengthBeats().inBeats()) };
        }

        const auto start = clip.getLoopStart().inSeconds();
        return { start, start + clip.getLoopLength().inSeconds() };
    }

    bool hasCrossfades (const te::AudioClipBase& clip)
    {
        return clip.getAutoCrossfade();
    }

    void setCrossfades (te::AudioClipBase& clip, bool shouldCrossfade)
    {
        if (clip.getAutoCrossfade() == shouldCrossfade)
            return;

        // Remembered: clips without it get crossfades when a song is opened.
        if (shouldCrossfade)
            clip.state.removeProperty (crossfadesOffId, &clip.edit.getUndoManager());
        else
            clip.state.setProperty (crossfadesOffId, true, &clip.edit.getUndoManager());

        clip.setAutoCrossfade (shouldCrossfade);
        clip.updateAutoCrossfadesAsync (true);
    }
}
