#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

namespace harmonia::spike
{
    struct Note
    {
        float startBeat = 0.0f;
        float lengthBeats = 1.0f;
        int pitch = 60;
        int colour = 0;
    };

    /** Notes sorted by start time, with a fast lookup of notes overlapping a beat range. */
    class NoteSet
    {
    public:
        /** Random but musically plausible notes spread across numBars bars of 4/4. */
        static NoteSet generate (int numNotes, int numBars, juce::uint32 seed);

        const std::vector<Note>& getNotes() const noexcept { return notes; }
        double getLengthBeats() const noexcept             { return lengthBeats; }

        /** Index range [first, last) of notes that may overlap [startBeat, endBeat). */
        std::pair<size_t, size_t> findOverlapping (double startBeat, double endBeat) const;

    private:
        std::vector<Note> notes;
        float maxNoteLength = 0.0f;
        double lengthBeats = 0.0;
    };
}
