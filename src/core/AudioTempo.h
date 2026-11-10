#pragma once

#include "core/BeatFinder.h"
#include "core/Tracktion.h"

/** Sound that follows the song: a loop recorded at its own tempo stretched (or squeezed) so that
    its beats fall on the song's, at any song tempo; and the pitch of a sound changed without
    changing its speed. Both are done by the engine while it plays (Signalsmith Stretch). */
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
        hundredths, and where its first beat is. Of the tempi that fit the same hits (twice or
        half as fast) the one nearest `nearBpm`. Nothing: no steady beat to hear in it (a pad, a
        phrase, one hit), or the file can't be read. Reads the file (its first two minutes): not
        for the audio thread. */
    std::optional<beat_finder::Beat> measure (te::Engine&, const juce::File&, double nearBpm);

    /** The loop tempo a file of `seconds` most likely has if it holds a whole number of bars
        (4, 8, 16... beats), the one nearest `songBpm` between 70 and 180 BPM. */
    std::optional<double> guessFromLength (double seconds, double songBpm);

    /** Makes the clip follow the song's tempo, its file being a loop at `loopBpm`: what it shows
        of the file keeps its beats (a bar of the loop stays a bar of the song), now and whenever
        the tempo changes. Nothing: back to playing the file at its own speed (same sound, its
        length in seconds again). Into the current undo step. */
    void setFollowsTempo (te::WaveAudioClip&, std::optional<double> loopBpm);

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

    /** The tempo the clip's file is taken to be at, while it follows the song. */
    std::optional<double> getLoopBpm (const te::AudioClipBase&);

    /** Seconds of the file played in a second of the song: 1 at its own speed, more when a loop is
        sped up to the song's tempo (for drawing the waveform under the beats it plays on). */
    double getPlaybackSpeed (const te::AudioClipBase&);

    /** "95", or "92.5" for a tempo between whole numbers. */
    juce::String formatBpm (double bpm);

    /** Higher or lower by semitones (within maxPitchSemitones), the speed unchanged. Undoable. */
    void setPitch (te::AudioClipBase&, float semitones);
    float getPitch (const te::AudioClipBase&);
}
