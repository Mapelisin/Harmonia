#include "NoteSet.h"

namespace harmonia::spike
{
    NoteSet NoteSet::generate (int numNotes, int numBars, juce::uint32 seed)
    {
        juce::Random random ((juce::int64) seed);
        NoteSet set;
        set.lengthBeats = numBars * 4.0;
        set.notes.reserve ((size_t) numNotes);

        const int numSixteenths = numBars * 16;
        static constexpr float lengths[] = { 0.25f, 0.5f, 0.5f, 1.0f, 1.0f, 2.0f, 4.0f };

        for (int i = 0; i < numNotes; ++i)
        {
            Note n;
            n.startBeat = (float) random.nextInt (numSixteenths) * 0.25f;
            n.lengthBeats = lengths[random.nextInt ((int) std::size (lengths))];
            // Centre around C4 with a spread of roughly three octaves.
            n.pitch = juce::jlimit (0, 127, 60 + (random.nextInt (19) - 9) + (random.nextInt (19) - 9));
            n.colour = random.nextInt (4);
            set.maxNoteLength = std::max (set.maxNoteLength, n.lengthBeats);
            set.notes.push_back (n);
        }

        std::sort (set.notes.begin(), set.notes.end(),
                   [] (const Note& a, const Note& b) { return a.startBeat < b.startBeat; });
        return set;
    }

    std::pair<size_t, size_t> NoteSet::findOverlapping (double startBeat, double endBeat) const
    {
        const auto byStart = [] (const Note& n, double beat) { return n.startBeat < beat; };

        auto first = std::lower_bound (notes.begin(), notes.end(), startBeat - maxNoteLength, byStart);
        auto last  = std::lower_bound (first, notes.end(), endBeat, byStart);

        return { (size_t) std::distance (notes.begin(), first), (size_t) std::distance (notes.begin(), last) };
    }
}
