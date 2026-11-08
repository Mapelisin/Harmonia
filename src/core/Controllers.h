#pragma once

#include "core/Tracktion.h"

/** What a MIDI clip plays besides its notes: the pitch-bend wheel, the mod wheel and the sustain
    pedal, as values over time. Values are 0..1 (pitch bend: 0.5 is the middle, no bend; the pedal:
    0 up, 1 down) and beats are from the start of the clip's content, as notes are. Changes go
    into the edit's undo history and to the clip's linked copies (core::clip_links). */
namespace harmonia::core::controllers
{
    enum class Controller
    {
        pitchBend,
        modWheel,
        sustain
    };

    const std::vector<Controller>& getAll();

    /** "Pitch bend", "Mod wheel", "Sustain pedal". */
    juce::String getName (Controller);

    /** Where the wheel or pedal is when nothing was played: bend in the middle, the others at 0. */
    float getDefault (Controller);

    /** True for the pitch bend: up and down from the middle. */
    bool isBipolar (Controller);

    /** True for the sustain pedal: only up or down. */
    bool isSwitch (Controller);

    /** For showing a value: "+50%" (bend), "64" (mod wheel, 0..127), "Down" / "Up" (pedal). */
    juce::String describe (Controller, float value);

    /** The controller a MIDI message moves, with the value it sets. */
    std::optional<std::pair<Controller, float>> fromMidi (const juce::MidiMessage&);

    /** The MIDI message that sets the controller to `value` (channel 1..16). */
    juce::MidiMessage toMidi (Controller, float value, int channel);

    struct Point
    {
        double beat = 0.0;
        float value = 0.0f;

        bool operator== (const Point&) const = default;
    };

    /** The clip's events of the controller, in time order. */
    std::vector<Point> getPoints (const te::MidiClip&, Controller);

    bool hasPoints (const te::MidiClip&, Controller);

    /** The value at `beat`: that of the last event at or before it, or the default. */
    float getValueAt (const te::MidiClip&, Controller, double beat);

    /** Draws a straight line from (beatA, valueA) to (beatB, valueB), in either order: what was
        there in between is replaced by small steps along it (one whenever the value changes; the
        pedal is down where the line is above the middle). */
    void drawLine (te::MidiClip&, Controller, double beatA, float valueA, double beatB, float valueB);

    /** Removes the events from `fromBeat` up to (not including) `toBeat`. */
    void erase (te::MidiClip&, Controller, double fromBeat, double toBeat);

    /** One event, replacing any at the same beat (for recording). */
    void add (te::MidiClip&, Controller, double beat, float value);

    /** Replaces all the controller's events with these (an event repeating the value before it is
        left out). */
    void set (te::MidiClip&, Controller, const std::vector<Point>&);

    /** The controller as the clip plays it in the song: its events in song beats, inside the
        clip, every time round if it loops. Where the clip (or a time round) begins after an event,
        the value that event set comes first, at that beat. */
    std::vector<Point> getPlayed (const te::MidiClip&, Controller);

    /** How finely drawn lines are stepped, in beats. */
    inline constexpr double drawStepBeats = 1.0 / 32.0;
}
