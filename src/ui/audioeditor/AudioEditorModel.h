#pragma once

#include "core/Clips.h"
#include "ui/arrangement/ArrangementModel.h"

namespace harmonia::ui::audioeditor
{
    /** What the audio clip editor (the lower panel of an audio track) edits, apart from drawing so
        it can be tested without a window: the arrangement's focused audio clip, where its sound
        lies (seconds of its file against beats of the song) and the commands of the editor.

        Changes marked "into the current undo step" are for drags: the caller begins the step
        (beginChange) and then changes as often as the mouse moves. The others are one undo
        step each. */
    class AudioEditorModel
    {
    public:
        explicit AudioEditorModel (arrangement::ArrangementModel&);

        arrangement::ArrangementModel& getArrangement() noexcept    { return arrangement; }
        te::Edit& getEdit() noexcept                                { return arrangement.getEdit(); }

        /** The clip shown (ArrangementModel::getFocusedAudioClip); none: nothing to edit. */
        te::AudioClipBase* getClip() const;

        //==============================================================================
        // Where the sound is. Seconds are seconds of the clip's file (0: where it begins, as
        // the clip plays it: a reversed clip's reversed copy); beats are song beats.

        /** How long the clip's file is; 0 without a clip or when the file can't be read. */
        double getFileSeconds() const;

        /** The part of the file the clip plays. */
        juce::Range<double> getPlayedSeconds() const;

        /** Where its fades end and start, in file seconds. */
        double getFadeInEndSeconds() const;
        double getFadeOutStartSeconds() const;

        double beatToFileSeconds (double beat) const;
        double fileSecondsToBeat (double seconds) const;

        /** Pixels per song beat at `pixelsPerSecond` of the file (faster for a loop sped up to the
            song's tempo): for the grid of the ruler. */
        double getPixelsPerBeat (double pixelsPerSecond) const;

        /** "Vocal take.wav  44.1 kHz  mono  6.00 s" (or that the file is missing). */
        juce::String describeFile() const;

        //==============================================================================
        // Changes

        void beginChange (const juce::String& name);

        /** Into the current undo step: the clip's start or end (its sound staying where it is in
            the song), at least `minLengthBeats` long and not before the song's start. */
        void trimStart (double beat, double minLengthBeats);
        void trimEnd (double beat, double minLengthBeats);

        /** Into the current undo step, in beats. */
        void setFadeInBeats (double);
        void setFadeOutBeats (double);

        /** Into the current undo step; up to `maxDb` (the handle +6 dB, the typed number +24). */
        void setGainDb (float, float maxDb = core::clips::maxGainDb);
        void setPitch (float semitones);

        /** Into the current undo step: the tempo of the loop (only while it follows the song). */
        void setLoopBpm (double);

        /** One undo step each. */
        bool normalize();
        void setReversed (bool);
        void setFadeShape (core::clips::FadeShape);
        void setFollowsTempo (bool);
        void halveLoopBpm();
        void doubleLoopBpm();

        /** Can the clip follow the tempo (a plain sound file clip)? */
        bool canFollowTempo() const;

    private:
        arrangement::ArrangementModel& arrangement;

        te::WaveAudioClip* getWaveClip() const;
    };
}
