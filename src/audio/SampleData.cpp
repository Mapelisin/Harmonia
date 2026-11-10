#include "audio/SampleData.h"

namespace harmonia::audio
{
    namespace
    {
        /** The file, open for reading: nullptr and the reason if it isn't a sound. */
        std::unique_ptr<juce::AudioFormatReader> open (te::Engine& engine, const juce::File& file, juce::String& error)
        {
            error = {};

            if (! file.existsAsFile())
            {
                error = "File not found: " + file.getFullPathName();
                return {};
            }

            auto& formats = engine.getAudioFileFormatManager().readFormatManager;
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            {
                error = "Can't read " + file.getFileName() + " as a sound";
                return {};
            }

            return reader;
        }
    }

    juce::String SampleData::check (te::Engine& engine, const juce::File& file)
    {
        juce::String error;
        open (engine, file, error);
        return error;
    }

    SampleData::Ptr SampleData::load (te::Engine& engine, const juce::File& file, juce::String& error)
    {
        const auto reader = open (engine, file, error);

        if (reader == nullptr)
            return {};

        const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
        const auto channels = std::min (2, (int) reader->numChannels);

        Ptr data (new SampleData());
        data->sampleRate = reader->sampleRate;
        data->cut = reader->lengthInSamples > length;
        data->audio.setSize (channels, length);

        if (! reader->read (&data->audio, 0, length, 0, true, channels > 1))
        {
            error = "Can't read " + file.getFileName() + " as a sound";
            return {};
        }

        return data;
    }

    float SampleData::getSampleAt (int channel, double position) const noexcept
    {
        const auto length = audio.getNumSamples();
        const auto* samples = audio.getReadPointer (std::min (channel, audio.getNumChannels() - 1));
        const auto index = (int) std::floor (position);
        const auto t = (float) (position - (double) index);

        const auto at = [samples, length] (int i)
        {
            return i >= 0 && i < length ? samples[i] : 0.0f;
        };

        const auto ym = at (index - 1), y0 = at (index), y1 = at (index + 1), y2 = at (index + 2);
        const auto c = 0.5f * (y1 - ym);
        const auto v = y0 - y1;
        const auto w = c + v;
        const auto a = w + v + 0.5f * (y2 - y0);
        const auto b = w + a;
        return (((a * t) - b) * t + c) * t + y0;
    }
}
