#pragma once

#include "core/Tracktion.h"

namespace harmonia::core
{
    /** All properties of one piano-roll note, as plain values.

        Storage: the note lives in Tracktion's NOTE ValueTree. Pitch, timing, velocity, release,
        mute and colour use Tracktion's own attributes; the FL-style per-note properties are
        extra `h*` attributes on the same node and are only written when they differ from the
        default, so plain notes stay small in the project file. */
    struct Note
    {
        int pitch = 60;                 // MIDI note number, 0..127
        double startBeat = 0.0;         // from the start of the clip's content, >= 0
        double lengthBeats = 1.0;       // >= 0
        int velocity = 100;             // 0..127
        int releaseVelocity = 64;       // 0..127 (note-off velocity)
        bool muted = false;
        int colour = 0;                 // colour group, also the note's MIDI channel offset: 0..15

        float pan = 0.0f;               // -1 (left) .. +1 (right)
        float modX = 0.5f;              // 0..1
        float modY = 0.5f;              // 0..1
        float finePitchCents = 0.0f;    // -1200..+1200
        bool slide = false;             // glides the pitch of an overlapping note of the same colour
        bool porta = false;             // portamento into this note
        int group = 0;                  // 0 = not grouped

        double getEndBeat() const noexcept     { return startBeat + lengthBeats; }

        /** Returns a copy with every value clamped to its valid range. */
        Note clamped() const;

        bool operator== (const Note&) const = default;
    };

    namespace note
    {
        constexpr int numColours = 16;
        constexpr float maxFinePitchCents = 1200.0f;

        /** Reads a stored note. Missing Harmonia attributes read as their defaults. */
        Note read (const te::MidiNote&);

        /** Writes `values` (clamped) to an existing note, recording undo actions in `um`. */
        void write (te::MidiNote&, const Note& values, juce::UndoManager* um);

        /** Creates a detached NOTE tree holding `values` (clamped), ready to add to a sequence. */
        juce::ValueTree createState (const Note& values);
    }
}
