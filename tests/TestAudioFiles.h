#pragma once

#include "TestSession.h"

namespace harmonia::test
{
    /** Writes a 16-bit WAV of a 220 Hz tone at `level`. With `beats`, the tone comes in short
        decaying hits every half second (a beat at 120 BPM), which makes a waveform worth looking at. */
    inline juce::File writeTone (const juce::File& file, double seconds, float level = 0.5f, int numChannels = 1,
                                 bool beats = false)
    {
        constexpr double sampleRate = 44100.0;
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (numChannels)
                                                       .withBitsPerSample (16));
        jassert (writer != nullptr);

        if (writer == nullptr)
            return {};

        const auto numSamples = (int) (seconds * sampleRate);
        const auto beatSamples = (int) (sampleRate / 2.0);
        juce::AudioBuffer<float> buffer (numChannels, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            auto value = level * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sampleRate);

            if (beats)
                value *= (float) std::exp (-6.0 * (i % beatSamples) / sampleRate) * (i / beatSamples % 4 == 0 ? 1.0f : 0.55f);

            for (int channel = 0; channel < numChannels; ++channel)
                buffer.setSample (channel, i, value);
        }

        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        return file;
    }

    /** A new empty folder in the temp directory, deleted afterwards (with the engine's open files
        let go first, or Windows would keep them). */
    struct TempFolder
    {
        TempFolder()    { folder.createDirectory(); }

        ~TempFolder()
        {
            session().getEngine().getAudioFileManager().releaseAllFiles();
            folder.deleteRecursively();
        }

        juce::File getFile (const juce::String& name) const   { return folder.getChildFile (name); }

        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getNonexistentChildFile ("harmonia-test-audio", {}, false);
    };
}
