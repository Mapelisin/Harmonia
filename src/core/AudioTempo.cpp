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

        /** The beats of its file a clip following the tempo shows, counted as its map counts
            them. (The engine counts the clip's offset in beats at the tempo where it starts.) */
        juce::Range<double> getShownBeats (const te::Clip& clip)
        {
            const auto from = clip.getPosition().getOffset().inSeconds() * getSongBeatsPerSecond (clip);
            return { from, from + getLengthBeats (clip) };
        }

        /** The clip from its start for `shown.getLength()` beats of the song (whatever the tempo
            does on the way), showing its file from the beat `shown.getStart()`. */
        void showBeats (te::AudioClipBase& clip, juce::Range<double> shown)
        {
            const auto& tempo = clip.edit.tempoSequence;
            const auto start = clip.getPosition().getStart();
            const auto end = tempo.toTime (tempo.toBeats (start) + te::BeatDuration::fromBeats (shown.getLength()));
            const auto offset = te::TimeDuration::fromSeconds (shown.getStart() / getSongBeatsPerSecond (clip));
            clip.setPosition (te::ClipPosition { { start, end }, offset });
        }

        /** What a clip plays of its file, in seconds of the file. */
        struct Played
        {
            juce::Range<double> shown;          // of a clip that doesn't repeat
            juce::Range<double> loop;           // of one that does: what repeats,
            double into = 0.0, rounds = 0.0;    // how far into it the clip starts, how many times round it goes
        };

        /** `map`: where the beats of its file are, if the clip follows the tempo. */
        Played getPlayed (const te::AudioClipBase& clip, const std::optional<BeatMap>& map)
        {
            Played played;

            if (map.has_value())
            {
                const auto beats = getShownBeats (clip);
                played.shown = { map->toSeconds (beats.getStart()), map->toSeconds (beats.getEnd()) };

                if (const auto length = clip.getLoopLengthBeats().inBeats(); clip.isLooping() && length > 0.0)
                {
                    const auto from = clip.getLoopStartBeats().inBeats();
                    played.loop = { map->toSeconds (from), map->toSeconds (from + length) };
                    played.into = beats.getStart() / length;
                    played.rounds = beats.getLength() / length;
                }
            }
            else
            {
                const auto position = clip.getPosition();
                const auto from = position.getOffset().inSeconds();
                played.shown = { from, from + position.getLength().inSeconds() };

                if (const auto length = clip.getLoopLength().inSeconds(); clip.isLooping() && length > 0.0)
                {
                    const auto loopStart = clip.getLoopStart().inSeconds();
                    played.loop = { loopStart, loopStart + length };
                    played.into = from / length;
                    played.rounds = played.shown.getLength() / length;
                }
            }

            return played;
        }

        /** Has a clip following the tempo play that of its file, its beats being where `map` has them. */
        void play (te::AudioClipBase& clip, const BeatMap& map, const Played& played)
        {
            if (played.loop.isEmpty())
            {
                showBeats (clip, { map.toBeats (played.shown.getStart()), map.toBeats (played.shown.getEnd()) });
                return;
            }

            // The same of the file repeats, as many times round as before.
            const juce::Range<double> beats (map.toBeats (played.loop.getStart()), map.toBeats (played.loop.getEnd()));
            clip.setLoopRangeBeats ({ te::BeatPosition::fromBeats (beats.getStart()), te::BeatPosition::fromBeats (beats.getEnd()) });
            showBeats (clip, { played.into * beats.getLength(), (played.into + played.rounds) * beats.getLength() });
        }

        /** Makes the clip follow the tempo with its beats where `given` has them (or go on
            following, with its beats there now). What it plays of its file stays. */
        void follow (te::WaveAudioClip& clip, const BeatMap& given)
        {
            // As it will be read back from the clip, by Harmonia and by the engine.
            const auto map = BeatMap::fromString (given.toString()).value_or (given);
            const auto previous = getBeatMap (clip);
            const auto played = getPlayed (clip, previous);
            const auto info = clip.getAudioFile().getInfo();
            const auto fileSeconds = info.getLengthInSeconds();
            auto* undo = &clip.edit.getUndoManager();

            // The engine takes the file for so many beats long: as many as the map counts in it.
            auto& loopInfo = clip.getLoopInfo();
            loopInfo.setBpm (fileSeconds > 0.0 ? map.toBeats (fileSeconds) * 60.0 / fileSeconds : map.getChanges().front().bpm, info);

            // It counts the loop's bars by its time signature, which a file without loop data
            // doesn't have (0 / 0, and counting bars of no beats never ends).
            if (loopInfo.getNumerator() <= 0 || loopInfo.getDenominator() <= 0)
            {
                loopInfo.setNumerator (4);
                loopInfo.setDenominator (4);
            }

            // Following first: until then the clip can't be longer than the file at its own speed.
            if (! previous.has_value())
                clip.setAutoTempo (true);

            if (map.isSteady())
                clip.state.removeProperty (te::IDs::sourceTempoMap, undo);
            else
                clip.state.setProperty (te::IDs::sourceTempoMap, map.toString(), undo);

            play (clip, map, played);
            audio_clips::prepareForPlayback (clip);
        }

        /** The map last read from a clip (a long one is asked for at every line of a drawing). */
        struct ReadMap
        {
            juce::String text;
            std::optional<BeatMap> map;
        };

        std::optional<BeatMap> readMap (const juce::String& text)
        {
            thread_local ReadMap last;

            if (last.text != text)
                last = { text, BeatMap::fromString (text) };

            return last.map;
        }
    }

    namespace
    {
        /** The hits of a sound file (both sides together), and how long the file is in seconds,
            if all of it was read (0: it is longer than is read). Nothing if it can't be read. */
        struct HeardHits
        {
            std::vector<beat_finder::Hit> hits;
            double wholeSeconds = 0.0;
        };

        std::optional<HeardHits> hearHits (te::Engine& engine, const juce::File& file)
        {
            // A whole take of a song, played by hand, is what drifts most.
            constexpr double longestSeconds = 300.0;
            std::unique_ptr<juce::AudioFormatReader> reader (te::AudioFileUtils::createReaderFor (engine, file));

            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
                return std::nullopt;

            const auto whole = reader->lengthInSamples <= (juce::int64) (longestSeconds * reader->sampleRate);
            const auto numSamples = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (longestSeconds * reader->sampleRate));
            const auto channels = std::min (2, (int) reader->numChannels);

            // Read a piece at a time.
            std::vector<float> sound ((size_t) numSamples);
            constexpr int pieceSamples = 1 << 16;
            juce::AudioBuffer<float> piece (channels, pieceSamples);

            for (int done = 0; done < numSamples; done += pieceSamples)
            {
                const auto count = std::min (pieceSamples, numSamples - done);

                if (! reader->read (&piece, 0, count, done, true, channels > 1))
                    return std::nullopt;

                if (channels > 1)
                {
                    piece.addFrom (0, 0, piece, 1, 0, count);
                    piece.applyGain (0, 0, count, 0.5f);
                }

                std::copy_n (piece.getReadPointer (0), count, sound.begin() + done);
            }

            // Of a file cut short here, the length says nothing about its beats.
            return HeardHits { beat_finder::findHits (sound.data(), numSamples, reader->sampleRate),
                               whole ? reader->lengthInSamples / reader->sampleRate : 0.0 };
        }

        /** Has a clip following the tempo take its beats to be where `given` has them, the clip
            staying where it is and as long. False if they were there already. */
        bool setBeats (te::WaveAudioClip& clip, const BeatMap& given)
        {
            const auto map = BeatMap::fromString (given.toString()).value_or (given);
            const auto text = map.isSteady() ? juce::String() : map.toString();
            const auto current = getBeatMap (clip);

            if (current.has_value() && current->toString() == map.toString())
                return false;

            // The engine takes the file for as many beats long as the map counts in it.
            const auto info = clip.getAudioFile().getInfo();

            if (const auto fileSeconds = info.getLengthInSeconds(); fileSeconds > 0.0)
                clip.getLoopInfo().setBpm (map.toBeats (fileSeconds) * 60.0 / fileSeconds, info);

            if (text.isEmpty())
                clip.state.removeProperty (te::IDs::sourceTempoMap, &clip.edit.getUndoManager());
            else
                clip.state.setProperty (te::IDs::sourceTempoMap, text, &clip.edit.getUndoManager());

            return true;
        }
    }

    std::optional<beat_finder::Beat> measure (te::Engine& engine, const juce::File& file, double nearBpm)
    {
        const auto heard = hearHits (engine, file);
        return heard.has_value() ? beat_finder::findBeat (heard->hits, heard->wholeSeconds, nearBpm) : std::nullopt;
    }

    std::vector<beat_finder::Hit> findHits (te::Engine& engine, const juce::File& file)
    {
        auto heard = hearHits (engine, file);
        return heard.has_value() ? std::move (heard->hits) : std::vector<beat_finder::Hit>();
    }

    bool moveBeat (te::WaveAudioClip& clip, double songBeat, double fileSeconds, double spanBeats)
    {
        const auto map = getBeatMap (clip);

        if (! map.has_value())
            return false;

        const auto beat = FileUnderSong (clip).toFileBeat (songBeat);
        const auto moved = map->withBeatAt (beat, fileSeconds, spanBeats);

        // (Put where it is already, to a hundredth of a millisecond, it isn't moved.)
        if (! moved.has_value() || std::abs (moved->toSeconds (beat) - map->toSeconds (beat)) < 1.0e-5)
            return false;

        return setBeats (clip, *moved);
    }

    bool resetBeat (te::WaveAudioClip& clip, double songBeat)
    {
        const auto map = getBeatMap (clip);

        if (! map.has_value())
            return false;

        const auto without = map->withoutChangeAt (FileUnderSong (clip).toFileBeat (songBeat));
        return without.has_value() && setBeats (clip, *without);
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
        const auto previous = getBeatMap (clip);

        if (loopBpm.has_value())
        {
            const auto bpm = juce::jlimit (minLoopBpm, maxLoopBpm, *loopBpm);
            const auto current = getLoopBpm (clip);

            // A sound followed beat by beat stays so: the same beats, counted anew.
            if (previous.has_value() && ! previous->isSteady() && current.has_value())
                follow (clip, previous->scaled (bpm / *current));
            else
                follow (clip, BeatMap (bpm));
        }
        else if (previous.has_value())
        {
            // What it plays of its file, at the file's own speed again.
            const auto played = getPlayed (clip, previous);
            const auto start = clip.getPosition().getStart();
            const auto seconds = [] (double s) { return te::TimeDuration::fromSeconds (s); };

            clip.setAutoTempo (false);
            clip.state.removeProperty (te::IDs::sourceTempoMap, &clip.edit.getUndoManager());

            if (played.loop.isEmpty())
            {
                clip.setPosition (te::ClipPosition { { start, start + seconds (played.shown.getLength()) }, seconds (played.shown.getStart()) });
            }
            else
            {
                // (The engine turns the beats that repeat into seconds by the song's tempo, not the sound's.)
                const auto length = played.loop.getLength();
                clip.setLoopRange ({ te::TimePosition::fromSeconds (played.loop.getStart()), te::TimePosition::fromSeconds (played.loop.getEnd()) });
                clip.setPosition (te::ClipPosition { { start, start + seconds (played.rounds * length) }, seconds (played.into * length) });
            }

            audio_clips::prepareForPlayback (clip);
        }
    }

    void setFollowsTempo (te::WaveAudioClip& clip, const beat_finder::Beat& beat)
    {
        if (beat.drifts())
        {
            if (const auto map = BeatMap::fromBeats (beat.beatSeconds))
            {
                follow (clip, *map);
                return;
            }
        }

        follow (clip, BeatMap (juce::jlimit (minLoopBpm, maxLoopBpm, beat.bpm)));
    }

    double startOnBeat (te::WaveAudioClip& clip, const beat_finder::Beat& beat)
    {
        const auto map = getBeatMap (clip);

        if (! map.has_value() || beat.bpm <= 0.0 || audio_clips::isLooping (clip))
            return 0.0;

        const auto shown = getShownBeats (clip);
        const auto shownFrom = map->toSeconds (shown.getStart());       // seconds of the file

        // The first beat at or after where the clip starts showing its file (a hair before counts).
        auto startsAt = 0.0;

        if (beat.drifts())
        {
            const auto& beats = beat.beatSeconds;
            const auto found = std::lower_bound (beats.begin(), beats.end(), shownFrom - 0.004);

            if (found != beats.end())
            {
                startsAt = *found;
            }
            else
            {
                // Past the last beat heard: on as the last two go.
                const auto step = beats.size() > 1 ? beats.back() - beats[beats.size() - 2] : 60.0 / beat.bpm;
                startsAt = beats.back() + std::ceil ((shownFrom - beats.back() - 0.004) / step) * step;
            }
        }
        else
        {
            const auto beatSeconds = 60.0 / beat.bpm;
            const auto steps = std::max (0.0, std::ceil ((shownFrom - beat.firstBeatSeconds - 0.004) / beatSeconds));
            startsAt = beat.firstBeatSeconds + steps * beatSeconds;
        }

        const auto hidden = startsAt - shownFrom;
        const auto from = map->toBeats (startsAt);

        if (hidden < 0.0005 || shown.getEnd() - from < 0.25)
            return 0.0;

        showBeats (clip, { from, shown.getEnd() });
        return hidden;
    }

    bool canSlide (const te::WaveAudioClip& clip)
    {
        return ! audio_clips::isLooping (clip) && clip.getAudioFile().getLength() > 0.0;
    }

    bool slide (te::WaveAudioClip& clip, double beats)
    {
        if (std::abs (beats) < 1.0e-9 || ! canSlide (clip))
            return false;

        // The sound later: the clip shows its file from an earlier moment; never from before
        // the file, nor from so late that nothing is left of it.
        const auto position = clip.getPosition();
        const auto map = getBeatMap (clip);

        if (! map.has_value())
        {
            // A clip that plays its file as it is: by the seconds those beats take in the song.
            const auto was = position.getOffset().inSeconds();
            const auto latest = std::max (0.0, clip.getAudioFile().getLength() / std::max (0.01, clip.getSpeedRatio()) - 0.05);
            const auto offset = juce::jlimit (0.0, std::max (latest, was), was - beats / getSongBeatsPerSecond (clip));

            if (std::abs (offset - was) < 1.0e-6)
                return false;

            clip.setPosition (te::ClipPosition { position.time, te::TimeDuration::fromSeconds (offset) });
            return true;
        }

        const auto shown = getShownBeats (clip);
        const auto latest = map->toBeats (std::max (0.0, clip.getAudioFile().getLength() - 0.05));
        const auto from = juce::jlimit (0.0, latest, shown.getStart() - beats);

        if (std::abs (map->toSeconds (from) - map->toSeconds (shown.getStart())) < 1.0e-6)
            return false;

        clip.setPosition (te::ClipPosition { position.time, te::TimeDuration::fromSeconds (from / getSongBeatsPerSecond (clip)) });
        return true;
    }

    Slip::Slip (te::WaveAudioClip& c)
        : clip (&c), from (c.getPosition())
    {
    }

    bool Slip::moveTo (double beats)
    {
        clip->setPosition (from);
        return slide (*clip, beats);
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

    std::optional<BeatMap> getBeatMap (const te::AudioClipBase& clip)
    {
        if (! clip.getAutoTempo())
            return std::nullopt;

        if (const auto text = clip.state.getProperty (te::IDs::sourceTempoMap).toString(); text.isNotEmpty())
            if (auto map = readMap (text))
                return map;

        if (const auto bpm = getLoopBpm (clip))
            return BeatMap (*bpm);

        return std::nullopt;
    }

    bool followsBeatByBeat (const te::AudioClipBase& clip)
    {
        const auto map = getBeatMap (clip);
        return map.has_value() && ! map->isSteady();
    }

    void setReversed (te::AudioClipBase& clip, bool shouldBeReversed)
    {
        if (clip.getIsReversed() == shouldBeReversed)
            return;

        // The engine turns a clip round as one whose sound keeps one tempo. Of a sound followed
        // beat by beat the beats are turned round here, and the clip plays what it played.
        const auto map = getBeatMap (clip);
        const auto fileSeconds = te::AudioFile (clip.edit.engine, clip.getOriginalFile()).getLength();

        if (! map.has_value() || map->isSteady() || fileSeconds <= 0.0)
        {
            clip.setIsReversed (shouldBeReversed);
            return;
        }

        const auto turnedRound = [fileSeconds] (juce::Range<double> seconds)
        {
            return juce::Range<double> (std::max (0.0, fileSeconds - seconds.getEnd()), std::max (0.0, fileSeconds - seconds.getStart()));
        };

        auto played = getPlayed (clip, map);
        played.shown = turnedRound (played.shown);
        played.loop = turnedRound (played.loop);

        clip.setIsReversed (shouldBeReversed);

        const auto backwards = map->reversed (fileSeconds);
        const auto turned = BeatMap::fromString (backwards.toString()).value_or (backwards);
        clip.state.setProperty (te::IDs::sourceTempoMap, turned.toString(), &clip.edit.getUndoManager());
        play (clip, turned, played);
    }

    //==============================================================================
    FileUnderSong::FileUnderSong (const te::AudioClipBase& clip)
        : tempo (clip.edit.tempoSequence), map (getBeatMap (clip))
    {
        const auto position = clip.getPosition();

        if (map.has_value())
        {
            // The file's beat 0: before the clip's start by its offset, or, of a clip that
            // repeats, before where its first time round starts by where the loop is in the file.
            const auto startBeat = tempo.toBeats (position.getStart()).inBeats();
            const auto offsetBeats = position.getOffset().inSeconds() * getSongBeatsPerSecond (clip);
            beatOfFileStart = startBeat - offsetBeats - (clip.isLooping() ? clip.getLoopStartBeats().inBeats() : 0.0);
            straight = map->isSteady();
        }
        else
        {
            timeOfFileStart = position.getStartOfSource()
                            - (clip.isLooping() ? te::toDuration (clip.getLoopStart()) : te::TimeDuration());
            straight = tempo.getNumTempos() <= 1;
        }
    }

    double FileUnderSong::toFileSeconds (double songBeat) const
    {
        if (map.has_value())
            return map->toSeconds (songBeat - beatOfFileStart);

        return (tempo.toTime (te::BeatPosition::fromBeats (songBeat)) - timeOfFileStart).inSeconds();
    }

    double FileUnderSong::toSongBeat (double fileSeconds) const
    {
        if (map.has_value())
            return beatOfFileStart + map->toBeats (fileSeconds);

        return tempo.toBeats (timeOfFileStart + te::TimeDuration::fromSeconds (fileSeconds)).inBeats();
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
