#pragma once

#include "core/Project.h"

/** A loop cut at its hits and put on the keys of a drum machine: each hit a sound of its own on a
    key (from C1 up), and a clip of notes that plays them where they were. Moving the notes about
    plays the loop another way; each hit can be given its own volume and tuning, or left out.

    The pieces are written as sound files next to the song's recordings, each from one hit to the
    next, at the file's own speed: a loop that was stretched to the song's tempo has its hits on
    the same beats, but each piece lasts as long as it did in the file. */
namespace harmonia::audio::slices
{
    // C1, where a drum kit begins; and how many keys a loop may take.
    inline constexpr int firstKey = 36;
    inline constexpr int mostSlices = 64;

    struct Slice
    {
        juce::File file;            // the piece of the sound
        double startBeat = 0.0;     // where it starts in the song
        double lengthBeats = 0.0;   // up to the next one
        int key = 0;
    };

    /** What a clip was cut into. */
    struct Sliced
    {
        juce::String name;                          // the clip's
        double startBeat = 0.0, endBeat = 0.0;      // where the clip was in the song
        float gainDb = 0.0f, tune = 0.0f;           // the clip's gain and pitch: every piece gets them
        std::vector<Slice> slices;
    };

    /** Cuts what the clip plays of its file at the hits heard in it (the strongest, if there are
        more than `mostSlices`), and writes each piece to a file. Nothing, `error` saying why, if
        it can't be: a clip that repeats, a file that can't be read, a sound with no hits to cut
        it at. Blocks while it reads and writes. Changes nothing in the song. */
    std::optional<Sliced> prepare (te::AudioClipBase&, juce::String& error);

    /** Gives the keys of the track's drum machine their pieces, and makes the clip of notes that
        plays them as the loop went. Into the current undo step. Returns the clip (nullptr if the
        track has no drum machine). */
    te::MidiClip* apply (core::Project&, te::AudioTrack& drumTrack, const Sliced&);
}
