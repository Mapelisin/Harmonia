#include "core/AudioTempo.h"
#include "core/AudioClips.h"

namespace harmonia::core::audio_tempo
{
    namespace
    {
        /** The song's beats per second where the clip starts (its offset is counted at that tempo). */
        double getSongBeatsPerSecond (const te::Clip& clip)
        {
            return clip.edit.tempoSequence.getBeatsPerSecondAt (clip.getPosition().getStart());
        }

        double getLengthBeats (const te::Clip& clip)
        {
            return (clip.getEndBeat() - clip.getStartBeat()).inBeats();
        }

        /** The clip from its start for that many beats of the song (whatever the tempo does on
            the way), its offset scaled by `offsetFactor`. */
        void setLengthBeats (te::AudioClipBase& clip, double beats, double offsetFactor)
        {
            const auto& tempo = clip.edit.tempoSequence;
            const auto position = clip.getPosition();
            const auto start = position.getStart();
            const auto end = tempo.toTime (tempo.toBeats (start) + te::BeatDuration::fromBeats (beats));
            const auto offset = te::TimeDuration::fromSeconds (position.getOffset().inSeconds() * offsetFactor);
            clip.setPosition (te::ClipPosition { { start, end }, offset });
        }

        /** The clip from its start for that many seconds, its offset scaled. */
        void setLengthSeconds (te::AudioClipBase& clip, double seconds, double offsetFactor)
        {
            const auto position = clip.getPosition();
            const auto start = position.getStart();
            const auto offset = te::TimeDuration::fromSeconds (position.getOffset().inSeconds() * offsetFactor);
            clip.setPosition (te::ClipPosition { { start, start + te::TimeDuration::fromSeconds (seconds) }, offset });
        }
    }

    std::optional<beat_finder::Beat> measure (te::Engine& engine, const juce::File& file, double nearBpm)
    {
        constexpr double longestSeconds = 120.0;
        std::unique_ptr<juce::AudioFormatReader> reader (te::AudioFileUtils::createReaderFor (engine, file));

        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            return std::nullopt;

        const auto whole = reader->lengthInSamples <= (juce::int64) (longestSeconds * reader->sampleRate);
        const auto numSamples = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (longestSeconds * reader->sampleRate));
        const auto channels = std::min (2, (int) reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, numSamples);

        if (! reader->read (&buffer, 0, numSamples, 0, true, channels > 1))
            return std::nullopt;

        // Both sides together.
        if (channels > 1)
        {
            buffer.addFrom (0, 0, buffer, 1, 0, numSamples);
            buffer.applyGain (0, 0, numSamples, 0.5f);
        }

        const auto hits = beat_finder::findHits (buffer.getReadPointer (0), numSamples, reader->sampleRate);

        // Of a file cut short here, the length says nothing about its beats.
        auto beat = beat_finder::findBeat (hits, whole ? reader->lengthInSamples / reader->sampleRate : 0.0, nearBpm);
        return beat;
    }

    std::optional<double> guessFromLength (double seconds, double songBpm)
    {
        if (seconds <= 0.0 || songBpm <= 0.0)
            return std::nullopt;

        std::optional<double> best;

        for (const auto beats : { 4.0, 8.0, 16.0, 32.0, 64.0 })
        {
            const auto bpm = beats * 60.0 / seconds;

            if (bpm < 70.0 || bpm > 180.0)
                continue;

            if (! best.has_value() || std::abs (std::log (bpm / songBpm)) < std::abs (std::log (*best / songBpm)))
                best = bpm;
        }

        return best;
    }

    std::optional<double> findLoopBpm (te::Engine& engine, const juce::File& file, double songBpm)
    {
        const te::AudioFile audioFile (engine, file);
        const auto info = audioFile.getInfo();
        const auto seconds = info.getLengthInSeconds();

        if (seconds <= 0.0 || info.loopInfo.isOneShot())
            return std::nullopt;

        // What the file says, or its name with a whole number of bars (read by the engine).
        if (info.loopInfo.getNumBeats() > 0.0)
            if (const auto bpm = info.loopInfo.getBpm (info); bpm >= minLoopBpm && bpm <= maxLoopBpm)
                return bpm;

        if (file.getFileNameWithoutExtension().containsIgnoreCase ("loop") && seconds >= 1.0 && seconds <= 60.0)
            return guessFromLength (seconds, songBpm);

        return std::nullopt;
    }

    void setFollowsTempo (te::WaveAudioClip& clip, std::optional<double> loopBpm)
    {
        const auto songBps = getSongBeatsPerSecond (clip);

        if (loopBpm.has_value())
        {
            const auto bpm = juce::jlimit (minLoopBpm, maxLoopBpm, *loopBpm);
            const auto info = clip.getAudioFile().getInfo();
            const auto previous = getLoopBpm (clip);
            auto& loopInfo = clip.getLoopInfo();
            loopInfo.setBpm (bpm, info);

            // The engine counts the loop's bars by its time signature, which a file without loop
            // data doesn't have (0 / 0, and counting bars of no beats never ends).
            if (loopInfo.getNumerator() <= 0 || loopInfo.getDenominator() <= 0)
            {
                loopInfo.setNumerator (4);
                loopInfo.setDenominator (4);
            }

            // A second of the file lasts bpm / 60 beats of the loop, played as beats of the song.
            // Following first: until then the clip can't be longer than the file at its own speed.
            if (previous.has_value())
            {
                setLengthBeats (clip, getLengthBeats (clip) * bpm / *previous, bpm / *previous);
            }
            else
            {
                const auto seconds = clip.getPosition().getLength().inSeconds();
                clip.setAutoTempo (true);
                setLengthBeats (clip, seconds * bpm / 60.0, bpm / 60.0 / songBps);
            }

            audio_clips::prepareForPlayback (clip);
        }
        else if (const auto previous = getLoopBpm (clip))
        {
            // Its beats played at the loop's own tempo again.
            const auto seconds = getLengthBeats (clip) / (*previous / 60.0);
            clip.setAutoTempo (false);
            setLengthSeconds (clip, seconds, songBps / (*previous / 60.0));
            audio_clips::prepareForPlayback (clip);
        }
    }

    double startOnBeat (te::WaveAudioClip& clip, const beat_finder::Beat& beat)
    {
        const auto bpm = getLoopBpm (clip);

        if (! bpm.has_value() || beat.bpm <= 0.0 || audio_clips::isLooping (clip))
            return 0.0;

        const auto songBps = getSongBeatsPerSecond (clip);
        const auto loopBps = *bpm / 60.0;
        const auto position = clip.getPosition();
        const auto shownFrom = position.getOffset().inSeconds() * songBps / loopBps;       // seconds of the file
        const auto beatSeconds = 60.0 / beat.bpm;

        // The first beat at or after where the clip starts showing its file (a hair before counts).
        const auto steps = std::max (0.0, std::ceil ((shownFrom - beat.firstBeatSeconds - 0.004) / beatSeconds));
        const auto startsAt = beat.firstBeatSeconds + steps * beatSeconds;
        const auto hidden = startsAt - shownFrom;
        const auto lengthBeats = getLengthBeats (clip) - hidden * loopBps;

        if (hidden < 0.0005 || lengthBeats < 0.25)
            return 0.0;

        const auto& tempo = clip.edit.tempoSequence;
        const auto start = position.getStart();
        const auto end = tempo.toTime (tempo.toBeats (start) + te::BeatDuration::fromBeats (lengthBeats));
        clip.setPosition (te::ClipPosition { { start, end }, te::TimeDuration::fromSeconds (startsAt * loopBps / songBps) });
        return hidden;
    }

    bool slide (te::WaveAudioClip& clip, double beats)
    {
        const auto bpm = getLoopBpm (clip);

        if (! bpm.has_value() || std::abs (beats) < 1.0e-9 || audio_clips::isLooping (clip))
            return false;

        const auto songBps = getSongBeatsPerSecond (clip);
        const auto loopBps = *bpm / 60.0;
        const auto position = clip.getPosition();
        const auto fileSeconds = clip.getAudioFile().getLength();

        // The sound later: the clip shows its file from an earlier moment; never from before
        // the file, nor from so late that nothing is left of it.
        const auto shownFrom = position.getOffset().inSeconds() * songBps / loopBps;
        const auto wanted = shownFrom - beats / loopBps;
        const auto startsAt = juce::jlimit (0.0, std::max (0.0, fileSeconds - 0.05), wanted);

        if (std::abs (startsAt - shownFrom) < 1.0e-6)
            return false;

        clip.setPosition (te::ClipPosition { position.time, te::TimeDuration::fromSeconds (startsAt * loopBps / songBps) });
        return true;
    }

    bool followsTempo (const te::AudioClipBase& clip)
    {
        return clip.getAutoTempo();
    }

    std::optional<double> getLoopBpm (const te::AudioClipBase& clip)
    {
        if (! clip.getAutoTempo())
            return std::nullopt;

        const auto bpm = clip.getLoopInfo().getBpm (clip.getAudioFile().getInfo());
        return bpm > 0.0 ? std::optional<double> (bpm) : std::nullopt;
    }

    double getPlaybackSpeed (const te::AudioClipBase& clip)
    {
        if (const auto bpm = getLoopBpm (clip))
            return getSongBeatsPerSecond (clip) / (*bpm / 60.0);

        return 1.0;
    }

    juce::String formatBpm (double bpm)
    {
        const auto rounded = std::round (bpm * 10.0) / 10.0;
        return std::abs (rounded - std::round (rounded)) < 0.05 ? juce::String (juce::roundToInt (rounded))
                                                               : juce::String (rounded, 1);
    }

    void setPitch (te::AudioClipBase& clip, float semitones)
    {
        clip.setPitchChange (juce::jlimit (-maxPitchSemitones, maxPitchSemitones, semitones));
        audio_clips::prepareForPlayback (clip);
    }

    float getPitch (const te::AudioClipBase& clip)
    {
        return clip.getPitchChange();
    }
}
