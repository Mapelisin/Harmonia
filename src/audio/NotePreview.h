#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Plays notes on a track's instrument right now, outside the song: clicking a key of the
        on-screen keyboard, or a note in the piano roll. Message thread only.

        Remembers which notes it started so that a track change or destruction never leaves
        a note hanging. */
    class NotePreview
    {
    public:
        NotePreview() = default;
        ~NotePreview();

        void setTrack (te::AudioTrack*);
        te::AudioTrack* getTrack() const;

        void noteOn (int pitch, int velocity);
        void noteOff (int pitch);
        void allNotesOff();

    private:
        te::Selectable::WeakRef track;
        te::MPESourceID sourceId = te::createUniqueMPESourceID();
        std::vector<int> soundingPitches;

        void send (const juce::MidiMessage&);

        JUCE_DECLARE_NON_COPYABLE (NotePreview)
    };
}
