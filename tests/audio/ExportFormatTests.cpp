#include <doctest.h>

#include "TestSession.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Mp3Encoder.h"
#include "core/Project.h"

using namespace harmonia;
using audio::ExportFormat;

namespace
{
    /** Two bars of A4 (440 Hz) on the built-in synth. */
    std::unique_ptr<core::Project> makeSong()
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        auto* clip = project->addMidiClip (track, 0.0, 8.0, "A4");
        REQUIRE (clip != nullptr);
        clip->getSequence().addNote (69, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (8.0), 100, 0, nullptr);
        return project;
    }

    struct Exported
    {
        juce::String formatName;
        int bitsPerSample = 0;
        bool usesFloat = false;
        double sampleRate = 0, seconds = 0;
        float rms = 0;
        double pitch = 0;
        juce::int64 bytes = 0;
    };

    /** The pitch of a second of sound from where it best repeats (300...700 Hz). */
    double pitchOf (const juce::AudioBuffer<float>& buffer, double sampleRate, double from)
    {
        const auto* x = buffer.getReadPointer (0);
        const auto start = (int) (from * sampleRate), length = (int) sampleRate / 2;
        auto bestLag = 0;
        auto best = -1.0;

        for (auto lag = (int) (sampleRate / 700.0); lag <= (int) (sampleRate / 300.0); ++lag)
        {
            double sum = 0.0, energy = 0.0;

            for (int i = start; i < start + length; ++i)
            {
                sum += (double) x[i] * x[i + lag];
                energy += (double) x[i + lag] * x[i + lag];
            }

            if (const auto score = energy > 0.0 ? sum / std::sqrt (energy) : 0.0; score > best)
            {
                best = score;
                bestLag = lag;
            }
        }

        return bestLag > 0 ? sampleRate / bestLag : 0.0;
    }

    Exported exportAs (core::Project& project, const ExportFormat& format)
    {
        const juce::TemporaryFile file (format.getExtension());
        const auto error = audio::exportSong (project.getEdit(), file.getFile(), format);
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);

        Exported result;
        result.formatName = reader->getFormatName();
        result.bitsPerSample = (int) reader->bitsPerSample;
        result.usesFloat = reader->usesFloatingPointData;
        result.sampleRate = reader->sampleRate;
        result.seconds = (double) reader->lengthInSamples / reader->sampleRate;
        result.bytes = file.getFile().getSize();

        // The note's first three seconds (the song is four, then the tail).
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) (3.0 * reader->sampleRate));
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        result.rms = buffer.getRMSLevel (0, (int) (0.5 * reader->sampleRate), (int) (2.0 * reader->sampleRate));
        result.pitch = pitchOf (buffer, reader->sampleRate, 1.0);
        return result;
    }

    float toDb (float gain)    { return juce::Decibels::gainToDecibels (gain); }
}

TEST_SUITE ("audio::ExportFormat")
{
    TEST_CASE ("an export format is written to the settings and read back; unknown text is WAV 24-bit")
    {
        for (const auto& format : ExportFormat::getChoices())
        {
            CAPTURE (format.toString());
            CHECK (ExportFormat::fromString (format.toString()) == format);
            CHECK (format.describe().isNotEmpty());
        }

        CHECK (ExportFormat::fromString ("nonsense") == ExportFormat());
        CHECK (ExportFormat().describe() == "WAV, 24-bit");
        CHECK (ExportFormat ({ ExportFormat::Type::flac, 16 }).getExtension() == ".flac");
        CHECK (ExportFormat ({ ExportFormat::Type::mp3, 16, 192 }).toString() == "mp3-192");
        CHECK (ExportFormat ({ ExportFormat::Type::mp3, 16, 192 }) != ExportFormat ({ ExportFormat::Type::mp3, 16, 320 }));

        const auto choices = ExportFormat::getChoices();
        CHECK (choices.getFirst() == ExportFormat());
        CHECK (choices.contains ({ ExportFormat::Type::wav, 16 }));
        CHECK (choices.contains ({ ExportFormat::Type::wav, 32 }));
        CHECK (choices.contains ({ ExportFormat::Type::flac, 24 }));
        CHECK (choices.contains ({ ExportFormat::Type::mp3, 16, 320 }) == audio::mp3::isAvailable (320));
    }

    TEST_CASE ("the song is exported as WAV of each bit depth and as FLAC, all sounding the same")
    {
        auto project = makeSong();
        const auto reference = exportAs (*project, {});

        CHECK (reference.bitsPerSample == 24);
        CHECK (reference.rms > 0.01f);
        CHECK (reference.pitch == doctest::Approx (440.0).epsilon (0.02));

        const auto wav16 = exportAs (*project, { ExportFormat::Type::wav, 16 });
        CHECK (wav16.bitsPerSample == 16);
        CHECK (toDb (wav16.rms) == doctest::Approx (toDb (reference.rms)).epsilon (0.01));

        const auto wav32 = exportAs (*project, { ExportFormat::Type::wav, 32 });
        CHECK (wav32.bitsPerSample == 32);
        CHECK (wav32.usesFloat);
        CHECK (wav32.rms == doctest::Approx (reference.rms).epsilon (0.001));

        for (const auto bits : { 16, 24 })
        {
            CAPTURE (bits);
            const auto flac = exportAs (*project, { ExportFormat::Type::flac, bits });
            CHECK (flac.formatName.containsIgnoreCase ("flac"));
            CHECK (flac.bitsPerSample == bits);
            CHECK (flac.seconds == doctest::Approx (reference.seconds).epsilon (0.001));
            CHECK (toDb (flac.rms) == doctest::Approx (toDb (reference.rms)).epsilon (0.01));
        }

        // Lossless, and smaller than the WAV.
        CHECK (exportAs (*project, { ExportFormat::Type::flac, 24 }).bytes < reference.bytes);
    }

    TEST_CASE ("the song is exported as MP3 that plays back as the song: as long, as loud, the same note")
    {
        if (! audio::mp3::isAvailable (320))
        {
            MESSAGE ("This computer has no MP3 encoder: MP3 export not checked");
            return;
        }

        auto project = makeSong();
        const auto reference = exportAs (*project, {});

        for (const auto kbps : { 320, 128 })
        {
            CAPTURE (kbps);
            const auto mp3 = exportAs (*project, { ExportFormat::Type::mp3, 16, kbps });
            CHECK (mp3.formatName.containsIgnoreCase ("mp3"));
            CHECK (audio::mp3::canUseSampleRate (mp3.sampleRate));

            // An MP3 starts and ends with a few milliseconds of the encoder's own.
            CHECK (std::abs (mp3.seconds - reference.seconds) < 0.1);
            CHECK (std::abs (toDb (mp3.rms) - toDb (reference.rms)) < 1.0f);
            CHECK (mp3.pitch == doctest::Approx (440.0).epsilon (0.02));

            // About the bit rate it was asked for (and so much smaller than the WAV).
            const auto kbpsMeasured = (double) mp3.bytes * 8.0 / 1000.0 / mp3.seconds;
            CHECK (kbpsMeasured == doctest::Approx ((double) kbps).epsilon (0.1));
        }
    }

    TEST_CASE ("a WAV that can't be an MP3 is refused with a reason, and no file is left")
    {
        const juce::TemporaryFile wav (".wav");
        const juce::TemporaryFile mp3 (".mp3");

        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wav.getFile());
            auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                                              .withSampleRate (96000.0)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
            REQUIRE (writer != nullptr);
            juce::AudioBuffer<float> silence (2, 9600);
            silence.clear();
            writer->writeFromAudioSampleBuffer (silence, 0, silence.getNumSamples());
        }

        CHECK (audio::mp3::encode (wav.getFile(), mp3.getFile(), 320).contains ("96000"));
        CHECK (! mp3.getFile().existsAsFile());
        CHECK (audio::mp3::encode (wav.getFile().getSiblingFile ("missing.wav"), mp3.getFile(), 320).startsWith ("Cannot read"));
    }
}
