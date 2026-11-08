#include "core/AudioClips.h"

namespace harmonia::core::audio_clips
{
    namespace
    {
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
}
