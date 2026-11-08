#include "core/AudioTempo.h"
#include "core/AudioClips.h"

namespace harmonia::core::audio_tempo
{
    namespace
    {
        /** The song's beats per second where the clip starts (the tempo is one for the whole song). */
        double getSongBeatsPerSecond (const te::Clip& clip)
        {
            return clip.edit.tempoSequence.getBeatsPerSecondAt (clip.getPosition().getStart());
        }

        /** Sets the clip's length and offset scaled by `factor`, its start kept. */
        void scaleTime (te::AudioClipBase& clip, double factor)
        {
            const auto position = clip.getPosition();
            const auto start = position.getStart();
            const auto length = te::TimeDuration::fromSeconds (position.getLength().inSeconds() * factor);
            const auto offset = te::TimeDuration::fromSeconds (position.getOffset().inSeconds() * factor);
            clip.setPosition (te::ClipPosition { { start, start + length }, offset });
        }
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
                scaleTime (clip, bpm / *previous);
            }
            else
            {
                clip.setAutoTempo (true);
                scaleTime (clip, bpm / 60.0 / songBps);
            }

            audio_clips::prepareForPlayback (clip);
        }
        else if (const auto previous = getLoopBpm (clip))
        {
            const auto factor = songBps / (*previous / 60.0);
            clip.setAutoTempo (false);
            scaleTime (clip, factor);
            audio_clips::prepareForPlayback (clip);
        }
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
