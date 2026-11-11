#pragma once

#include "core/Controllers.h"
#include "core/Note.h"
#include "core/Project.h"

namespace harmonia::core
{
    /** Where what is played on a track goes while a take is recorded, wherever in the song it is
        played: into the MIDI clip the track has at that beat or, where the track has none, into
        a clip made for the take. A take makes one clip per empty stretch of the track: the clip
        begins at the bar of the first thing played there and grows by whole bars to hold what is
        played after it (and before it, when the song goes round a loop), never into a clip that
        was there already.

        Positions are song beats. Every change goes into the current undo step: the caller starts
        the gesture, so a whole take is one step. */
    class TakeClips
    {
    public:
        TakeClips (Project&, te::AudioTrack&);

        /** Whether notes can be recorded on the track: an instrument track that is in the song
            (not an audio track, a return or a group). */
        static bool canRecordOn (const te::AudioTrack&);

        /** A played note. It ends where its clip has to end (another clip follows). A note the
            clip already has (the same pitch at the same start: the same part played again on
            the loop's next time round) is left out. Returns the clip the note is in, nullptr if
            it went nowhere. */
        te::MidiClip* addNote (const Note& songNote);

        /** A value of a wheel, a pedal or another controller, played at `songBeat`. Returns the
            clip it is in, nullptr if it went nowhere. */
        te::MidiClip* addController (controllers::Controller, double songBeat, float value);

        /** The clips the take has made that the song still has, in the order they were made. */
        juce::Array<te::MidiClip*> getMadeClips() const;

    private:
        Project& project;
        te::AudioTrack::Ptr track;
        juce::ReferenceCountedArray<te::MidiClip> made;

        struct Place
        {
            te::MidiClip* clip = nullptr;
            double contentBeat = 0.0;       // where the song beat is in the clip's content
            double roomBeats = 0.0;         // how long a note starting there can be
        };

        Place placeFor (double beat, double endBeat);
        te::MidiClip* clipFor (double beat, double endBeat);
        bool isMade (const te::MidiClip*) const;

        JUCE_DECLARE_NON_COPYABLE (TakeClips)
    };
}
