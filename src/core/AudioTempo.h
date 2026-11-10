#pragma once

#include "core/BeatFinder.h"
#include "core/BeatMap.h"
#include "core/Tracktion.h"

/** Sound that follows the song: a loop recorded at its own tempo stretched (or squeezed) so that
    its beats fall on the song's, at any song tempo; and the pitch of a sound changed without
    changing its speed. Both are done by the engine while it plays (Signalsmith Stretch).

    A sound that doesn't keep one tempo (somebody playing) follows beat by beat: the clip keeps
    where each beat of its file is (a BeatMap), and the engine plays each on a beat of the song
    (patch 0004). */
namespace harmonia::core::audio_tempo
{
    /** The tempi a loop may have. */
    inline constexpr double minLoopBpm = 50.0, maxLoopBpm = 250.0;

    /** How far the pitch of a clip can go, in semitones either way. */
    inline constexpr float maxPitchSemitones = 24.0f;

    /** A sound file's own tempo when it is a loop: from what the file says about itself (ACID or
        Apple loop data), its name ("Drums 95 BPM", "loop_120_Am"), or, for a file whose name says
        it's a loop but not its tempo, its length (the whole number of bars nearest the song's
        tempo). Nothing for anything else: a drum hit or a sung phrase keeps its own speed. */
    std::optional<double> findLoopBpm (te::Engine&, const juce::File&, double songBpm);

    /** The beat of a sound file measured from its sound (core::beat_finder): its tempo, to
        hundredths, and where its first beat is; of a sound whose tempo drifts, where each beat
        is. Of the tempi that fit the same hits (twice or half as fast) the one nearest `nearBpm`.
        Nothing: no beat to hear in it (a pad, a phrase, one hit), or the file can't be read.
        Reads the file (its first five minutes): not for the audio thread. */
    std::optional<beat_finder::Beat> measure (te::Engine&, const juce::File&, double nearBpm);

    /** The loop tempo a file of `seconds` most likely has if it holds a whole number of bars
        (4, 8, 16... beats), the one nearest `songBpm` between 70 and 180 BPM. */
    std::optional<double> guessFromLength (double seconds, double songBpm);

    /** Makes the clip follow the song's tempo, its file being a loop at `loopBpm`: what it shows
        of the file keeps its beats (a bar of the loop stays a bar of the song), now and whenever
        the tempo changes. A clip that follows beat by beat goes on doing so, its beats counted
        anew (twice the tempo: twice the beats). Nothing: back to playing the file at its own
        speed (same sound, its length in seconds again). Into the current undo step. */
    void setFollowsTempo (te::WaveAudioClip&, std::optional<double> loopBpm);

    /** Makes the clip follow the song's tempo as its beat was measured: at the one tempo found,
        or, for a sound whose tempo drifts, beat by beat. Into the current undo step. */
    void setFollowsTempo (te::WaveAudioClip&, const beat_finder::Beat&);

    /** A clip following the tempo starts on a beat of its sound: the first beat of `beat` that
        the clip shows (what the file has before it is hidden, not lost: the left edge shows it
        again). The clip stays where it starts in the song and ends where its sound ended. Not
        for a clip that repeats. Into the current undo step. Returns the seconds of sound hidden. */
    double startOnBeat (te::WaveAudioClip&, const beat_finder::Beat& beat);

    /** Slides the sound of a clip following the tempo under the clip by `beats` of the song
        (earlier: negative), the clip staying where it is: for a first beat taken a step off.
        As far as the file goes. Into the current undo step. Returns false if it couldn't move. */
    bool slide (te::WaveAudioClip&, double beats);

    bool followsTempo (const te::AudioClipBase&);

    /** The tempo the clip's file is taken to be at, while it follows the song (of a sound whose
        tempo drifts: on the whole). */
    std::optional<double> getLoopBpm (const te::AudioClipBase&);

    /** Where the beats of the clip's file are, while it follows the song. */
    std::optional<BeatMap> getBeatMap (const te::AudioClipBase&);

    /** Whether the clip follows beat by beat (its sound doesn't keep one tempo). */
    bool followsBeatByBeat (const te::AudioClipBase&);

    /** The hits of a sound file, in order (core::beat_finder): what a beat moved by hand is put
        on. Reads the file (its first five minutes): not for the audio thread. */
    std::vector<beat_finder::Hit> findHits (te::Engine&, const juce::File&);

    /** Puts a beat of the sound of a clip following the tempo elsewhere in its file: the moment
        `fileSeconds` plays on `songBeat` from now on. The beats of the song `spanBeats` either
        side stay over what they were over, and so does all beyond them; the clip stays where
        it is and as long. A sound that kept one tempo is followed beat by beat from then on.
        Into the current undo step. False if nothing changed (not following; the start of the
        file; already there). */
    bool moveBeat (te::WaveAudioClip&, double songBeat, double fileSeconds, double spanBeats = 1.0);

    /** Lets go of a beat put elsewhere (or found off the others): the sound's tempo no longer
        changes under `songBeat`. Into the current undo step. False if it didn't change there. */
    bool resetBeat (te::WaveAudioClip&, double songBeat);

    /** Plays the clip's sound backwards, or forwards again (core::clips::setReversed is how it
        is asked for): of a sound followed beat by beat the beats are turned round with it. Into
        the current undo step. */
    void setReversed (te::AudioClipBase&, bool);

    /** Which moment of its file a clip has under which beat of the song: for drawing the
        waveform under the beats it plays on, and for showing the song's beats over the file.
        The clip is taken to go on before its start and after its end. One that repeats: as its
        first time round lies in the song (where the others begin: core::audio_clips).
        Made for a paint or a command, not kept: the clip may change. */
    class FileUnderSong
    {
    public:
        explicit FileUnderSong (const te::AudioClipBase&);

        double toFileSeconds (double songBeat) const;
        double toSongBeat (double fileSeconds) const;

        /** Of a clip following the tempo: which beat of its file (as its BeatMap counts them) is
            under a beat of the song. */
        double toFileBeat (double songBeat) const noexcept  { return songBeat - beatOfFileStart; }

        /** The same length of file under every beat of the song: one stretch of the waveform
            can be drawn under any stretch of beats. Not so for a sound followed beat by beat,
            nor for one playing at its own speed through a change of the song's tempo. */
        bool isStraight() const noexcept                    { return straight; }

    private:
        const te::TempoSequence& tempo;
        std::optional<BeatMap> map;         // of a clip that follows the tempo
        double beatOfFileStart = 0.0;       // following: the song beat the file's beat 0 is under
        te::TimePosition timeOfFileStart;   // not following: the song time the file's start is at
        bool straight = true;
    };

    /** "95", or "92.5" for a tempo between whole numbers. */
    juce::String formatBpm (double bpm);

    /** Higher or lower by semitones (within maxPitchSemitones), the speed unchanged. Undoable. */
    void setPitch (te::AudioClipBase&, float semitones);
    float getPitch (const te::AudioClipBase&);
}
