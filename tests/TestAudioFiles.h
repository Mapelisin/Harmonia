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

    /** Writes a 24-bit mono WAV with a short click (a kilohertz, gone in a few milliseconds) at
        each of `seconds`, at `levels` (as many, or all 0.9), and `tailSeconds` after the last. */
    inline juce::File writeClicks (const juce::File& file, const std::vector<double>& seconds, double tailSeconds,
                                   const std::vector<float>& levels = {})
    {
        constexpr double sampleRate = 44100.0;
        file.deleteFile();
        juce::AudioBuffer<float> buffer (1, (int) std::round ((seconds.back() + tailSeconds) * sampleRate));
        buffer.clear();

        for (size_t n = 0; n < seconds.size(); ++n)
        {
            const auto start = (int) std::round (seconds[n] * sampleRate);
            const auto level = n < levels.size() ? levels[n] : 0.9f;

            for (int i = 0; i < (int) (0.01 * sampleRate) && start + i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, start + i, level * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / sampleRate)
                                                      * (float) std::exp (-i / (0.003 * sampleRate)));
        }

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sampleRate)
                                                                                     .withNumChannels (1)
                                                                                     .withBitsPerSample (24));
        jassert (writer != nullptr);

        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());

        return file;
    }

    /** A loop of `beats` beats at `bpm`, a click on each: named "Drums 95 BPM.wav", a file that
        says its tempo. */
    inline juce::File writeLoop (const juce::File& file, double bpm, int beats)
    {
        std::vector<double> seconds;

        for (int beat = 0; beat < beats; ++beat)
            seconds.push_back (beat * 60.0 / bpm);

        return writeClicks (file, seconds, 60.0 / bpm);
    }

    /** Where the clicks of a sound file start, in seconds: louder than `threshold` after a tenth
        of a second that wasn't. */
    inline std::vector<double> findClicks (const juce::File& file, float threshold = 0.2f)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        std::vector<double> clicks;

        if (reader == nullptr)
            return clicks;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        auto lastLoud = -(int) reader->sampleRate;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (std::abs (buffer.getSample (0, i)) <= threshold)
                continue;

            if (i - lastLoud > (int) (0.1 * reader->sampleRate))
                clicks.push_back (i / reader->sampleRate);

            lastLoud = i;
        }

        return clicks;
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

        // Never the same path twice: the engine remembers files by path (their length...), and a
        // new file where an older test left another would look like the old one.
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getNonexistentChildFile ("harmonia-test-audio-" + juce::Uuid().toString().substring (0, 12), {}, false);
    };
}
