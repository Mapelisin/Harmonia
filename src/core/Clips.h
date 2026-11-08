#pragma once

#include "core/Project.h"

namespace harmonia::core::clips
{
    /** Beats per bar of the song (the first time signature). */
    double getBeatsPerBar (const te::Edit&);

    /** Song beat where the clip's content beat 0 is (clip start minus its offset). */
    double getContentStartBeat (const te::Clip&);

    double getStartBeat (const te::Clip&);
    double getEndBeat (const te::Clip&);

    /** The track's first MIDI clip (the one the piano roll edits until the arrangement exists). */
    te::MidiClip* getMainClip (te::AudioTrack&);

    /** The track's first MIDI clip; creates an empty one at the start of the song if there is none. */
    te::MidiClip* getOrAddMainClip (Project&, te::AudioTrack&, double lengthBeats);

    /** Lengthens `clip` and its linked copies so that content up to `contentEndBeat` plays.
        The new end is rounded up to a whole bar. Clips are never shortened. Undoable. */
    void extendToCover (te::MidiClip& clip, double contentEndBeat);

    //==============================================================================
    // The arrangement. Positions are song beats; every change is undoable.

    /** The track's MIDI clips in time order. */
    juce::Array<te::MidiClip*> getMidiClips (te::AudioTrack&);

    /** The track's MIDI and audio clips in time order. */
    juce::Array<te::Clip*> getClips (te::AudioTrack&);

    /** An audio clip (plays a file), not a MIDI clip. */
    bool isAudio (const te::Clip&);

    /** False once the clip has been deleted (also by undo) or its track has. */
    bool isInEdit (const te::Clip&);

    /** Where the last clip of the song ends (0 for an empty song). */
    double getSongEndBeat (const te::Edit&);

    /** Puts the clip at `startBeat` (at least 0) on `track`, with its length and content. */
    void move (te::Clip&, double startBeat, te::AudioTrack& track);

    /** Moves the clip's start, its notes (or audio) staying where they are in the song: hides or
        shows the beginning. Not before the content start, and at least `minLengthBeats` long. */
    void setStart (te::Clip&, double startBeat, double minLengthBeats);

    /** Moves the clip's end, its content staying where it is. At least `minLengthBeats` long;
        an audio clip not past the end of its file. */
    void setEnd (te::Clip&, double endBeat, double minLengthBeats);

    /** A copy of the clip on its track at `startBeat`. A linked copy (MIDI clips only; audio
        clips are always copied independently) shares its notes with the original. */
    te::Clip* duplicate (te::Clip&, double startBeat, bool linked);

    /** Deletes clips. A link group left with one clip stops being a group. */
    void remove (const juce::Array<te::Clip*>&);

    /** Cuts the clip in two at `beat`: the clip keeps the part before, the returned clip is the part
        after. Nothing moves in the song and nothing is lost: the hidden parts come back if a half is
        made longer again. The halves of a linked MIDI clip stay linked. nullptr unless `beat` is
        inside the clip. */
    te::Clip* split (te::Clip&, double beat);

    /** Can a clip like this (MIDI or audio, see te::ClipCopy::getState) go on this track? MIDI clips
        go on instrument tracks, audio clips on audio tracks, nothing on return tracks. */
    bool canTake (const te::AudioTrack&, const te::ClipCopy&);

    /** Inserts a new clip from a copy (what Ctrl+C keeps) on `track` at `startBeat`. The new clip is
        independent: a copy of a linked MIDI clip doesn't join its group. nullptr if !canTake. */
    te::Clip* insertCopy (te::AudioTrack& track, const te::ClipCopy&, double startBeat);

    //==============================================================================
    // Audio clips: fades and gain (undoable; the caller starts the gesture). Fade lengths are
    // given in beats; the engine keeps them in seconds, so they stay as long when the tempo changes.

    /** How a fade goes: straight, smooth at both ends, fast at first, slow at first. */
    enum class FadeShape { linear, sCurve, fast, slow };

    /** Fade shapes in menu order, and their names ("Linear", "S-curve"...). */
    const std::vector<FadeShape>& getFadeShapes();
    juce::String getFadeShapeName (FadeShape);

    double getFadeInBeats (const te::AudioClipBase&);
    double getFadeOutBeats (const te::AudioClipBase&);

    /** The fade at the clip's start: at least 0, never into the fade at the end. */
    void setFadeInBeats (te::AudioClipBase&, double beats);

    /** The fade at the clip's end: at least 0, never into the fade at the start. */
    void setFadeOutBeats (te::AudioClipBase&, double beats);

    /** The shape of the clip's fades (the one at the start; both are set together). */
    FadeShape getFadeShape (const te::AudioClipBase&);
    void setFadeShape (te::AudioClipBase&, FadeShape);

    /** Gain of the clip before its track's effects and fader; at most +6 dB, -100 is silence. */
    float getGainDb (const te::AudioClipBase&);
    void setGainDb (te::AudioClipBase&, float db);

    inline constexpr float maxGainDb = 6.0f, silentGainDb = -100.0f;
}
