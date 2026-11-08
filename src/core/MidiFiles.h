#pragma once

#include "core/Controllers.h"
#include "core/Note.h"
#include "core/Project.h"

/** Standard MIDI Files (.mid): reading them into notes, and writing a clip or the whole song.
    Positions are beats (quarter notes), so a file plays the same at any tempo. */
namespace harmonia::core::midi_files
{
    /** .mid / .midi by its extension. */
    bool isMidiFile (const juce::File&);

    /** One track of a MIDI file. */
    struct Part
    {
        juce::String name;              // the track's name in the file (may be empty)
        std::vector<Note> notes;        // from the file's start; the MIDI channel is the colour
        double endBeat = 0.0;           // where its last note ends
        bool pedalled = false;          // the sustain pedal lengthened some of its notes

        /** How the pitch-bend and mod wheels moved, from the file's start (all channels together;
            the pedal is in the notes' lengths). */
        std::map<controllers::Controller, std::vector<controllers::Point>> controllerEvents;
    };

    struct Contents
    {
        std::vector<Part> parts;                // the tracks that have notes, in file order
        std::optional<double> bpm;              // the file's first tempo
        std::optional<int> beatsPerBar;         // its first time signature, if it is over 4
    };

    /** Reads a MIDI file. Fails (nullopt, the reason in `error`) if it can't be read, uses SMPTE
        time instead of beats, or has no notes. The sustain pedal (controller 64) is written into
        the notes' lengths: a key let go while the pedal is down sounds on until the pedal comes up
        (or the same key is struck again), so a piano part sounds as it was played on any
        instrument. */
    std::optional<Contents> read (const juce::File&, juce::String& error);

    /** Writes the clip's notes as it plays them (from its start to its end; muted notes left out),
        its pitch bend, mod wheel and pedal (on the notes' channels) and the song's tempo. Returns
        what went wrong, or an empty string. */
    juce::String writeClip (te::MidiClip&, const juce::File&);

    /** Writes the song: a MIDI track for each instrument track with notes, named after it, its
        clips' notes and controllers where they play in the song; the tempo and the time signature. Returns what went
        wrong ("Nothing to export..." without notes), or an empty string. */
    juce::String writeSong (Project&, const juce::File&);

    inline constexpr int ticksPerBeat = 960;
}
