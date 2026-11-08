#pragma once

#include "core/Note.h"

namespace harmonia::core
{
    /** The per-note values shown in the piano roll's lower panel. */
    enum class NoteProperty
    {
        velocity,
        release,
        pan,
        modX,
        modY,
        finePitch
    };

    namespace note_property
    {
        const std::vector<NoteProperty>& getAll();
        juce::String getName (NoteProperty);

        /** True for properties whose neutral value is in the middle (pan, fine pitch). */
        bool isBipolar (NoteProperty);

        /** The value scaled to 0..1, as drawn in the panel. */
        float get (const Note&, NoteProperty);

        /** Sets the value from 0..1 (clamped). */
        void set (Note&, NoteProperty, float normalised);

        /** 0..1 value of a new note. */
        float getDefault (NoteProperty);

        /** Human-readable value, e.g. "Velocity 100" or "Pan 25% L". */
        juce::String describe (const Note&, NoteProperty);
    }

    /** Pure note transformations. Times are in beats. */
    namespace note_ops
    {
        struct BeatRange
        {
            double start = 0.0, end = 0.0;
            double getLength() const noexcept       { return end - start; }
        };

        /** Earliest start and latest end (empty range for no notes). */
        BeatRange getSpan (const std::vector<Note>&);

        /** Snaps the start to the nearest grid line; with `startsOnly` false the end is snapped too
            (never shorter than one grid step). */
        Note quantized (const Note&, double gridBeats, bool startsOnly);

        /** Splits a note at `beat` (strictly inside it) into two notes with the same properties. */
        std::pair<Note, Note> split (const Note&, double beat);

        /** Limits a transposition so every note stays within 0..127; the chord shape is kept. */
        int limitTransposition (const std::vector<Note>&, int semitones);

        /** Limits a move in time so no note starts before `minStart`. */
        double limitMove (const std::vector<Note>&, double deltaBeats, double minStart = 0.0);

        /** How far Duplicate moves the copy: the span from the beat the notes start in,
            rounded up to whole beats. */
        double getDuplicateOffset (const std::vector<Note>&);
    }
}
