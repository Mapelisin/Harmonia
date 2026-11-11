#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "audio/ExportTail.h"
#include "audio/Sends.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace export_tail = audio::export_tail;
using audio::ExportFormat;

namespace
{
    struct Sound
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        int bits = 0;
        juce::String format;

        double getSeconds() const   { return buffer.getNumSamples() / sampleRate; }

        /** The loudest sample between two moments, in dBFS. */
        float getPeakDb (double from, double to) const
        {
            const auto start = juce::jlimit (0, buffer.getNumSamples(), (int) (from * sampleRate));
            const auto end = juce::jlimit (start, buffer.getNumSamples(), (int) (to * sampleRate));
            return juce::Decibels::gainToDecibels (buffer.getMagnitude (start, end - start), -200.0f);
        }
    };

    Sound read (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        Sound sound;
        sound.sampleRate = reader->sampleRate;
        sound.bits = (int) reader->bitsPerSample;
        sound.format = reader->getFormatName();
        sound.buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&sound.buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        return sound;
    }

    /** A 24-bit stereo WAV: a tone at `level` for `toneSeconds`, dying away over `decaySeconds`
        to nothing, then silence up to `seconds`. */
    juce::File writeRinging (const juce::File& file, double seconds, double toneSeconds, double decaySeconds, float level)
    {
        constexpr double sampleRate = 44100.0;
        juce::AudioBuffer<float> buffer (2, (int) (seconds * sampleRate));
        buffer.clear();

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto t = i / sampleRate;

            if (t > toneSeconds + decaySeconds)
                break;

            const auto gain = t <= toneSeconds ? 1.0 : std::pow (10.0, -4.0 * (t - toneSeconds) / decaySeconds);    // 80 dB down over the decay
            const auto value = level * (float) (gain * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t));
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

TEST_SUITE ("audio: an export rings out")
{
    TEST_CASE ("where the sound of a file ends: 60 dB under its loudest moment, loud or quiet; nothing after the song's end is its end")
    {
        test::TempFolder temp;
        auto& engine = test::session().getEngine();

        // A tone for a second, dying away by 80 dB over the next two: 60 dB down a second and a half in.
        const auto loud = writeRinging (temp.getFile ("Loud.wav"), 10.0, 1.0, 2.0, 0.5f);
        const auto loudEnd = export_tail::findLastSound (engine, loud, 1.0);
        REQUIRE (loudEnd.has_value());
        CHECK (*loudEnd == doctest::Approx (2.5).epsilon (0.03));

        // The same 30 dB quieter rings as long.
        const auto quiet = writeRinging (temp.getFile ("Quiet.wav"), 10.0, 1.0, 2.0, 0.5f * 0.0316f);
        const auto quietEnd = export_tail::findLastSound (engine, quiet, 1.0);
        REQUIRE (quietEnd.has_value());
        CHECK (*quietEnd > 1.9);
        CHECK (*quietEnd < 2.6);

        // Silence after where it is looked for from: that moment itself.
        CHECK (export_tail::findLastSound (engine, loud, 5.0).value_or (0.0) == doctest::Approx (5.0));
        CHECK_FALSE (export_tail::findLastSound (engine, temp.getFile ("None.wav"), 0.0).has_value());

        // How long the export is: half a second after the last sound, or after the song if nothing rings.
        CHECK (export_tail::getLength (1.0, 2.5) == doctest::Approx (3.0));
        CHECK (export_tail::getLength (4.0, 4.0) == doctest::Approx (4.5));
    }

    TEST_CASE ("a file cut to length in each format: as it was up to there, the last half second fading to nothing")
    {
        test::TempFolder temp;
        auto& engine = test::session().getEngine();
        const auto source = writeRinging (temp.getFile ("Source.wav"), 6.0, 6.0, 0.0, 0.5f);
        const auto original = read (source);

        for (const auto& format : { ExportFormat { ExportFormat::Type::wav, 24 }, ExportFormat { ExportFormat::Type::wav, 16 },
                                    ExportFormat { ExportFormat::Type::wav, 32 }, ExportFormat { ExportFormat::Type::flac, 24 } })
        {
            CAPTURE (format.describe().toStdString());
            const auto target = temp.getFile ("Cut " + format.toString() + format.getExtension());
            REQUIRE (export_tail::writeCut (engine, source, target, format, 2.0).isEmpty());

            const auto cut = read (target);
            CHECK (cut.getSeconds() == doctest::Approx (2.0).epsilon (0.0005));
            CHECK (cut.bits == format.bitDepth);
            CHECK (cut.buffer.getNumChannels() == 2);
            CHECK (cut.format.containsIgnoreCase (format.type == ExportFormat::Type::flac ? "flac" : "wav"));

            // The same samples before the fade (to what 16 bits can hold); nothing at the very end.
            const auto tolerance = format.bitDepth == 16 ? 1.0e-4f : 1.0e-6f;

            for (const auto i : { 0, 1000, 44100, 60000 })
                CHECK (std::abs (cut.buffer.getSample (0, i) - original.buffer.getSample (0, i)) < tolerance);

            CHECK (cut.getPeakDb (1.0, 1.4) == doctest::Approx (-6.0f).epsilon (0.02));
            CHECK (cut.getPeakDb (1.74, 1.76) == doctest::Approx (-12.0f).epsilon (0.05));      // half way down the fade
            CHECK (cut.getPeakDb (1.999, 2.0) < -55.0f);
            CHECK (std::abs (cut.buffer.getSample (0, cut.buffer.getNumSamples() - 1)) < 1.0e-4f);
        }

        // Asked for more than there is: all of it.
        const auto all = temp.getFile ("All.wav");
        REQUIRE (export_tail::writeCut (engine, source, all, {}, 20.0).isEmpty());
        CHECK (read (all).getSeconds() == doctest::Approx (6.0).epsilon (0.0005));
    }

    TEST_CASE ("the song is exported until its reverb has died away; without a reverb, a moment after its last clip; a part of it, exactly")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->setTempoBpm (120.0);
        auto& track = *project->getTracks().getFirst();
        core::tracks::setKind (track, core::tracks::Kind::audio);

        // Two seconds of tone: the song ends at 2 s.
        juce::String error;
        REQUIRE (project->addAudioClip (track, test::writeTone (temp.getFile ("Tone.wav"), 2.0, 0.4f), 0.0, error) != nullptr);

        const auto dryFile = temp.getFile ("Dry.wav");
        REQUIRE (audio::exportWav (project->getEdit(), dryFile).isEmpty());
        const auto dry = read (dryFile);
        CHECK (dry.getSeconds() == doctest::Approx (2.0 + export_tail::keptSeconds).epsilon (0.02));

        // Into a reverb: the export goes on while it rings, and ends in silence.
        auto* reverb = audio::sends::addReturnTrack (*project, audio::sends::Preset::reverb, error);
        REQUIRE (reverb != nullptr);
        REQUIRE (audio::sends::setLevelDb (track, *reverb, 0.0f));

        const auto wetFile = temp.getFile ("Wet.wav");
        REQUIRE (audio::exportWav (project->getEdit(), wetFile).isEmpty());
        const auto wet = read (wetFile);
        CHECK (wet.getSeconds() > dry.getSeconds() + 0.3);
        CHECK (wet.getSeconds() < 2.0 + export_tail::longestSeconds);

        const auto end = wet.getSeconds();
        const auto peak = wet.getPeakDb (0.0, 2.0);
        CHECK (wet.getPeakDb (2.2, 2.6) > peak - 50.0f);                // the reverb is there after the clip
        CHECK (wet.getPeakDb (end - 0.05, end) < peak - 60.0f);         // and has gone by the end
        CHECK (wet.getPeakDb (end - 0.001, end) < -80.0f);

        // In another format it rings about as long (where a tail that wavers dips under silence
        // for good is a matter of a few samples' worth).
        const auto flacFile = temp.getFile ("Wet.flac");
        REQUIRE (audio::exportSong (project->getEdit(), flacFile, { ExportFormat::Type::flac, 16 }).isEmpty());
        CHECK (std::abs (read (flacFile).getSeconds() - wet.getSeconds()) < 0.5);

        // A part of the song is that part and no more.
        const auto partFile = temp.getFile ("Part.wav");
        const te::TimeRange part (te::TimePosition::fromSeconds (0.5), te::TimePosition::fromSeconds (1.5));
        REQUIRE (audio::exportWav (project->getEdit(), partFile, part).isEmpty());
        CHECK (read (partFile).getSeconds() == doctest::Approx (1.0).epsilon (0.001));

        // Nothing is left next to the exported files (what was rendered past the end is gone).
        CHECK (temp.getFile ("Dry.wav").getParentDirectory().findChildFiles (juce::File::findFiles, false).size() == 5);
    }
}
