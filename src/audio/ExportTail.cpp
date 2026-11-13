#include "audio/ExportTail.h"
#include "audio/Mp3Encoder.h"

namespace harmonia::audio::export_tail
{
    namespace
    {
        constexpr int blockSamples = 1 << 15;

        std::unique_ptr<juce::AudioFormatReader> open (te::Engine& engine, const juce::File& file)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (te::AudioFileUtils::createReaderFor (engine, file));

            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
                return nullptr;

            return reader;
        }

        /** Copies the first `numSamples` of the reader to a new file of `format` (WAV or FLAC),
            each sample times `gain`, the last `fadeSamples` fading to nothing. Float sound that
            becomes 16-bit is dithered, as the engine does when it renders 16-bit itself. */
        juce::String copy (te::Engine& engine, juce::AudioFormatReader& reader, const juce::File& target, const ExportFormat& format,
                           juce::int64 numSamples, juce::int64 fadeSamples, float gain)
        {
            target.deleteFile();
            auto& formats = engine.getAudioFileFormatManager();
            auto* audioFormat = format.type == ExportFormat::Type::flac ? formats.getFlacFormat() : formats.getWavFormat();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (target);

            if (audioFormat == nullptr || ! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
                return "Cannot write " + target.getFullPathName();

            const auto channels = (int) reader.numChannels;
            auto writer = audioFormat->createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                                    .withSampleRate (reader.sampleRate)
                                                                    .withNumChannels (channels)
                                                                    .withBitsPerSample (format.bitDepth));

            if (writer == nullptr)
                return "Cannot write " + target.getFullPathName();

            juce::AudioBuffer<float> buffer (channels, blockSamples);
            const auto fadeFrom = numSamples - fadeSamples;
            std::vector<te::Ditherer> ditherers ((size_t) (reader.usesFloatingPointData && format.bitDepth == 16 ? channels : 0));

            for (auto& ditherer : ditherers)
                ditherer.reset (format.bitDepth);

            for (juce::int64 done = 0; done < numSamples; done += blockSamples)
            {
                const auto count = (int) std::min<juce::int64> (blockSamples, numSamples - done);

                if (! reader.read (&buffer, 0, count, done, true, true))
                    return "Export failed: " + target.getFullPathName();

                if (gain != 1.0f)
                    buffer.applyGain (0, count, gain);

                // The end of the block is in the fade: each sample by how far it is from the end.
                if (fadeSamples > 0 && done + count > fadeFrom)
                {
                    for (int i = (int) std::max<juce::int64> (0, fadeFrom - done); i < count; ++i)
                    {
                        const auto fade = (float) ((double) (numSamples - (done + i)) / (double) fadeSamples);

                        for (int channel = 0; channel < channels; ++channel)
                            buffer.setSample (channel, i, buffer.getSample (channel, i) * fade);
                    }
                }

                for (size_t channel = 0; channel < ditherers.size(); ++channel)
                    ditherers[channel].process (buffer.getWritePointer ((int) channel), count);

                if (! writer->writeFromAudioSampleBuffer (buffer, 0, count))
                    return "Export failed: " + target.getFullPathName() + " (is the disk full?)";
            }

            return {};
        }
    }

    std::optional<double> findLastSound (te::Engine& engine, const juce::File& file, double fromSeconds)
    {
        const auto reader = open (engine, file);

        if (reader == nullptr)
            return std::nullopt;

        const auto from = std::max<juce::int64> (0, (juce::int64) (fromSeconds * reader->sampleRate));
        const auto channels = (int) reader->numChannels;
        juce::AudioBuffer<float> buffer (channels, blockSamples);

        // How loud the song gets says how quiet silence is.
        auto peak = 0.0f;

        for (juce::int64 start = 0; start < reader->lengthInSamples; start += blockSamples)
        {
            const auto count = (int) std::min<juce::int64> (blockSamples, reader->lengthInSamples - start);

            if (! reader->read (&buffer, 0, count, start, true, true))
                return std::nullopt;

            peak = std::max (peak, buffer.getMagnitude (0, count));
        }

        const auto silence = juce::Decibels::decibelsToGain (juce::jlimit (quietestSilenceDb, loudestSilenceDb,
                                                                           juce::Decibels::gainToDecibels (peak) - silenceBelowPeakDb));

        // From the end back, a block at a time.
        for (auto end = reader->lengthInSamples; end > from; end -= blockSamples)
        {
            const auto start = std::max (from, end - blockSamples);
            const auto count = (int) (end - start);

            if (! reader->read (&buffer, 0, count, start, true, true))
                return std::nullopt;

            for (int i = count; --i >= 0;)
                for (int channel = 0; channel < channels; ++channel)
                    if (std::abs (buffer.getSample (channel, i)) > silence)
                        return (double) (start + i) / reader->sampleRate;
        }

        return fromSeconds;
    }

    double getLength (double songEndSeconds, double lastSoundSeconds)
    {
        return std::max (songEndSeconds, lastSoundSeconds) + keptSeconds;
    }

    juce::String writeCut (te::Engine& engine, const juce::File& wav, const juce::File& target, const ExportFormat& format, double seconds,
                           float gainDb)
    {
        const auto reader = open (engine, wav);

        if (reader == nullptr)
            return "Export failed: " + target.getFullPathName();

        const auto numSamples = std::min (reader->lengthInSamples, (juce::int64) std::llround (seconds * reader->sampleRate));
        const auto fadeSamples = std::min (numSamples, (juce::int64) (keptSeconds * reader->sampleRate));

        const auto gain = gainDb == 0.0f ? 1.0f : juce::Decibels::decibelsToGain (gainDb);

        if (format.type != ExportFormat::Type::mp3)
            return copy (engine, *reader, target, format, numSamples, fadeSamples, gain);

        // An MP3 is made of a WAV cut to length.
        const juce::TemporaryFile cut (wav);

        if (const auto error = copy (engine, *reader, cut.getFile(), { ExportFormat::Type::wav, 32 }, numSamples, fadeSamples, gain); error.isNotEmpty())
            return error;

        return mp3::encode (cut.getFile(), target, format.kbps);
    }
}
