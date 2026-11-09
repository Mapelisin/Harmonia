#pragma once

#include "core/Note.h"
#include "core/Project.h"

namespace harmonia::core::clips
{
    /** Beats per bar of the song (the first time signature). */
    double getBeatsPerBar (const te::Edit&);

    /** Song beat where the clip's content beat 0 is (clip start minus its offset). For a looping
        clip, where it is in the loop's first time round (see getLoopRange). */
    double getContentStartBeat (const te::Clip&);

    double getStartBeat (const te::Clip&);
    double getEndBeat (const te::Clip&);

    /** The track's first MIDI clip (the one the piano roll edits until the arrangement exists). */
    te::MidiClip* getMainClip (te::AudioTrack&);

    /** The track's first MIDI clip; creates an empty one at the start of the song if there is none. */
    te::MidiClip* getOrAddMainClip (Project&, te::AudioTrack&, double lengthBeats);

    /** Lengthens `clip` and its linked copies so that content up to `contentEndBeat` plays.
        The new end is rounded up to a whole bar. Clips are never shortened. Undoable.
        A looping clip gets a longer loop instead (and is made as long as the loop if shorter). */
    void extendToCover (te::MidiClip& clip, double contentEndBeat);

    //==============================================================================
    // Looping MIDI clips: the content between the loop's start and end plays over and over for
    // as long as the clip is; made longer, the clip repeats it. Every change is undoable.

    bool isLooping (const te::Clip&);

    /** On: what the clip shows now becomes the loop. Off: the content plays once, from where the
        loop started (the clip keeps its length). */
    void setLooping (te::MidiClip&, bool);

    /** Content beats of the loop (empty if the clip doesn't loop). */
    juce::Range<double> getLoopRange (const te::Clip&);

    /** Song beats of the loop's first time round (empty if the clip doesn't loop). It may begin
        before the clip if the clip's start was trimmed. */
    juce::Range<double> getLoopSongRange (const te::Clip&);

    /** A longer or shorter loop (at least a 1/16 beat), from the same start. */
    void setLoopLengthBeats (te::MidiClip&, double lengthBeats);

    /** Writes every time round as notes of their own: the clip stops looping and sounds the same.
        A linked clip is made unique first (its copies don't change). */
    void bakeLoop (te::MidiClip&);

    /** A note as the clip plays it in the song. */
    struct PlayedNote
    {
        Note note;                      // as written in the clip (content beats)
        double startBeat = 0.0;         // song beats, inside the clip
        double endBeat = 0.0;
        int repeat = 0;                 // which time round the loop (0 if not looping)
        bool cutAtClipStart = false;    // began before the clip: its start is hidden
    };

    /** Every note the clip plays, in song beats (looping clips: every time round), trimmed to
        the clip. Muted notes too. */
    std::vector<PlayedNote> getPlayedNotes (const te::MidiClip&);

    //==============================================================================
    // The arrangement. Positions are song beats; every change is undoable.

    /** The track's MIDI clips in time order. */
    juce::Array<te::MidiClip*> getMidiClips (te::AudioTrack&);

    /** The track's MIDI and audio clips in time order (not the file a frozen track plays). */
    juce::Array<te::Clip*> getClips (te::AudioTrack&);

    /** The clip a frozen track plays in place of its own (audio::freeze): kept out of getClips,
        so nothing shows or edits it. */
    bool isFreezeClip (const te::Clip&);
    void setFreezeClip (te::Clip&);

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

    inline constexpr float maxGainDb = 6.0f, silentGainDb = -100.0f, maxNormalizeGainDb = 24.0f;

    /** Gain of the clip before its track's effects and fader; -100 is silence. At most `maxDb`:
        the gain handle goes up to +6 dB, a typed gain up to +24 (as far as normalize goes). */
    float getGainDb (const te::AudioClipBase&);
    void setGainDb (te::AudioClipBase&, float db, float maxDb = maxGainDb);

    /** The loudest point of what the clip plays from its file, in dBFS, before the clip's gain
        (-100 for silence); nothing if the file can't be read (yet). */
    std::optional<float> getPeakDb (const te::AudioClipBase&);

    /** Sets the clip's gain so that its loudest point is at `targetDb` (gain from -100 up to
        +24 dB: a quiet recording can be brought up further than the gain handle goes). False if
        the file can't be read or is silent. */
    bool normalize (te::AudioClipBase&, float targetDb = -0.1f);

    /** Plays the sound backwards. The engine writes a reversed copy in the background: until
        it is there (`isReversing`) the clip is silent. */
    bool isReversed (const te::AudioClipBase&);
    void setReversed (te::AudioClipBase&, bool);
    bool isReversing (const te::AudioClipBase&);

    //==============================================================================
    /** The colour the user chose for the clip; none means its track's colour. (Tracktion gives
        every clip a colour of its own when it is made, so this is kept apart from it.) */
    std::optional<juce::Colour> getColour (const te::Clip&);

    /** Undoable (the caller starts the gesture). Saved with the project; copies keep it. */
    void setColour (te::Clip&, std::optional<juce::Colour>);
}
