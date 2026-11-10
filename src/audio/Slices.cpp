#include "audio/Slices.h"
#include "audio/DrumSamples.h"
#include "audio/Instruments.h"
#include "core/AudioClips.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"

namespace harmonia::audio::slices
{
    namespace clips = core::clips;

    namespace
    {
        // A piece is never shorter than this, and a hit this near the start of the clip is its start.
        constexpr double shortestSeconds = 0.04;

        // Each piece comes in and goes out over a moment: a cut in the middle of a wave clicks.
        constexpr double fadeInSeconds = 0.001, fadeOutSeconds = 0.005;

        /** Where the clip's sound is cut, in seconds of its file: its start, then the hits heard
            in what it plays (the strongest, if there are too many). In order. */
        std::vector<double> findCuts (te::AudioClipBase& clip, juce::Range<double> played)
        {
            auto hits = core::audio_tempo::findHits (clip.edit.engine, clip.getCurrentSourceFile());

            hits.erase (std::remove_if (hits.begin(), hits.end(), [played] (const core::beat_finder::Hit& hit)
            {
                return hit.seconds < played.getStart() + shortestSeconds || hit.seconds > played.getEnd() - shortestSeconds;
            }), hits.end());

            // Too close after another, a hit is part of it: the stronger of two wins its place.
            std::vector<core::beat_finder::Hit> apart;

            for (const auto& hit : hits)
            {
                if (! apart.empty() && hit.seconds - apart.back().seconds < shortestSeconds)
                {
                    if (hit.strength > apart.back().strength)
                        apart.back().strength = hit.strength;

                    continue;
                }

                apart.push_back (hit);
            }

            if ((int) apart.size() > mostSlices - 1)
            {
                std::sort (apart.begin(), apart.end(), [] (const auto& a, const auto& b) { return a.strength > b.strength; });
                apart.resize ((size_t) mostSlices - 1);
                std::sort (apart.begin(), apart.end(), [] (const auto& a, const auto& b) { return a.seconds < b.seconds; });
            }

            std::vector<double> cuts { played.getStart() };

            for (const auto& hit : apart)
                cuts.push_back (hit.seconds);

            return cuts;
        }

        /** A file for the `number`th piece that isn't there yet. */
        juce::File getPieceFile (const juce::File& folder, const juce::String& name, int number)
        {
            const auto base = juce::File::createLegalFileName (name + " slice " + juce::String (number).paddedLeft ('0', 2));
            auto file = folder.getChildFile (base + ".wav");

            for (int copy = 2; file.exists(); ++copy)
                file = folder.getChildFile (base + " (" + juce::String (copy) + ").wav");

            return file;
        }

        /** Writes `numSamples` of the reader from `start` to a WAV, coming in and going out over a moment. */
        bool writePiece (juce::AudioFormatReader& reader, juce::int64 start, int numSamples, const juce::File& file)
        {
            const auto channels = std::min (2, (int) reader.numChannels);
            juce::AudioBuffer<float> buffer (channels, numSamples);

            if (! reader.read (&buffer, 0, numSamples, start, true, channels > 1))
                return false;

            const auto fadeIn = std::min (numSamples / 2, (int) (fadeInSeconds * reader.sampleRate));
            const auto fadeOut = std::min (numSamples / 2, (int) (fadeOutSeconds * reader.sampleRate));
            buffer.applyGainRamp (0, fadeIn, 0.0f, 1.0f);
            buffer.applyGainRamp (numSamples - fadeOut, fadeOut, 1.0f, 0.0f);

            file.getParentDirectory().createDirectory();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                           .withSampleRate (reader.sampleRate)
                                                           .withNumChannels (channels)
                                                           .withBitsPerSample (24));

            return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        }
    }

    std::optional<Sliced> prepare (te::AudioClipBase& clip, juce::String& error)
    {
        auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack());

        if (track == nullptr)
            return std::nullopt;

        if (core::audio_clips::isLooping (clip))
        {
            error = "A clip that repeats can't be sliced: stop it repeating first (its menu)";
            return std::nullopt;
        }

        auto& engine = clip.edit.engine;
        const auto source = clip.getCurrentSourceFile();
        std::unique_ptr<juce::AudioFormatReader> reader (te::AudioFileUtils::createReaderFor (engine, source));

        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        {
            error = "This sound can't be read: " + source.getFileName();
            return std::nullopt;
        }

        // What the clip plays of the file, and where in the song.
        const core::audio_tempo::FileUnderSong under (clip);
        const auto fileSeconds = reader->lengthInSamples / reader->sampleRate;
        Sliced sliced;
        sliced.name = clip.getName();
        sliced.startBeat = clips::getStartBeat (clip);
        sliced.endBeat = clips::getEndBeat (clip);
        sliced.gainDb = clips::getGainDb (clip);
        sliced.tune = core::audio_tempo::getPitch (clip);

        const juce::Range<double> played (juce::jlimit (0.0, fileSeconds, under.toFileSeconds (sliced.startBeat)),
                                          juce::jlimit (0.0, fileSeconds, under.toFileSeconds (sliced.endBeat)));
        const auto cuts = findCuts (clip, played);

        if (cuts.size() < 2)
        {
            error = "No hits to cut it at were heard in this sound";
            return std::nullopt;
        }

        const auto folder = engine.getEngineBehaviour().getFileForNewAudioRecording (*track, ".wav").getParentDirectory();

        for (size_t i = 0; i < cuts.size(); ++i)
        {
            const auto from = cuts[i], to = i + 1 < cuts.size() ? cuts[i + 1] : played.getEnd();
            const auto start = (juce::int64) std::llround (from * reader->sampleRate);
            const auto numSamples = (int) std::min<juce::int64> (std::llround ((to - from) * reader->sampleRate), reader->lengthInSamples - start);

            if (numSamples < 16)
                continue;

            Slice slice;
            slice.file = getPieceFile (folder, sliced.name, (int) sliced.slices.size() + 1);
            slice.key = firstKey + (int) sliced.slices.size();
            slice.startBeat = under.toSongBeat (from);
            slice.lengthBeats = under.toSongBeat (to) - slice.startBeat;

            if (! writePiece (*reader, start, numSamples, slice.file))
            {
                // What was written so far is nobody's.
                for (const auto& written : sliced.slices)
                    written.file.deleteFile();

                error = "A piece of the sound couldn't be written to " + slice.file.getFullPathName();
                return std::nullopt;
            }

            sliced.slices.push_back (std::move (slice));
        }

        return sliced;
    }

    te::MidiClip* apply (core::Project& project, te::AudioTrack& drumTrack, const Sliced& sliced)
    {
        auto* drums = instruments::playsDrumKit (drumTrack) ? instruments::get (drumTrack) : nullptr;

        if (drums == nullptr || sliced.slices.empty())
            return nullptr;

        std::vector<std::pair<int, juce::File>> samples;

        for (const auto& slice : sliced.slices)
            samples.push_back ({ slice.key, slice.file });

        drum_kit::setSamples (*drums, samples);

        // As loud and as high as the clip played them.
        for (const auto& slice : sliced.slices)
        {
            if (std::abs (sliced.gainDb) > 0.01f)
                drum_kit::setSampleGainDb (*drums, slice.key, sliced.gainDb);

            if (std::abs (sliced.tune) > 0.001f)
                drum_kit::setSampleTune (*drums, slice.key, sliced.tune);
        }

        auto* clip = project.addMidiClip (drumTrack, sliced.startBeat, sliced.endBeat - sliced.startBeat, sliced.name);

        if (clip == nullptr)
            return nullptr;

        std::vector<core::Note> notes;

        for (const auto& slice : sliced.slices)
        {
            core::Note note;
            note.pitch = slice.key;
            note.startBeat = slice.startBeat - sliced.startBeat;
            note.lengthBeats = std::max (1.0 / 32.0, slice.lengthBeats);
            notes.push_back (note);
        }

        core::NoteEditor (*clip).add (notes);
        return clip;
    }
}
