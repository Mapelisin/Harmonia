#pragma once

#include "core/Tracktion.h"

/** What a MIDI clip plays besides its notes: the pitch-bend wheel, the pressure on the keys
    (aftertouch) and any numbered MIDI controller (the mod wheel, the pedals, expression, volume,
    a knob of the keyboard...), as values over time. Values are 0..1 (pitch bend and pan: the
    middle is the rest; a pedal: 0 up, 1 down) and beats are from the start of the clip's content,
    as notes are. Changes go into the edit's undo history and to the clip's linked copies
    (core::clip_links). */
namespace harmonia::core::controllers
{
    /** One of them: a numbered MIDI controller ("CC"), the pitch bend or the aftertouch. */
    struct Controller
    {
        /** The controller's MIDI number 0..127; for the pitch bend and the aftertouch, the number
            Tracktion's clips keep them under. */
        int type = 1;

        static const Controller pitchBend, modWheel, sustain, expression, volume, pan, aftertouch;

        /** A numbered controller. */
        static constexpr Controller cc (int number) noexcept    { return { std::clamp (number, 0, 127) }; }

        constexpr bool isCc() const noexcept                    { return type >= 0 && type < 128; }

        auto operator<=> (const Controller&) const = default;
    };

    inline constexpr Controller Controller::pitchBend   { te::MidiControllerEvent::pitchWheelType };
    inline constexpr Controller Controller::aftertouch  { te::MidiControllerEvent::channelPressureType };
    inline constexpr Controller Controller::modWheel    { 1 };
    inline constexpr Controller Controller::volume      { 7 };
    inline constexpr Controller Controller::pan         { 10 };
    inline constexpr Controller Controller::expression  { 11 };
    inline constexpr Controller Controller::sustain     { 64 };

    /** The ones offered first: pitch bend, mod wheel, sustain pedal, expression, volume, pan,
        aftertouch. */
    const std::vector<Controller>& getCommon();

    /** Every one a clip can hold: the common ones, then the other numbered controllers by number
        (see isPlainNumber). */
    const std::vector<Controller>& getAll();

    /** True for a controller number that is a knob, a wheel or a pedal: not the numbers MIDI
        keeps for other jobs (bank select, data entry, (N)RPN, the channel-mode messages such as
        "all notes off"). Those aren't recorded, read from files or assigned to anything. */
    bool isPlainNumber (int number);

    /** The ones the clip has events of: the common ones first, then by number. */
    std::vector<Controller> getUsed (const te::MidiClip&);

    /** "Pitch bend", "Mod wheel", "Sustain pedal", "Expression", "Aftertouch", "CC 74 Brightness",
        "CC 21". */
    juce::String getName (Controller);

    /** The name with the controller's number, for a list to choose from: "Mod wheel (CC 1)". */
    juce::String getLongName (Controller);

    /** Where the controller is when nothing was played: bend and pan in the middle, expression
        full, volume at MIDI's usual 100, the others at 0. */
    float getDefault (Controller);

    /** True for the pitch bend and the pan: up and down from the middle. */
    bool isBipolar (Controller);

    /** True for the pedals and the other on/off controllers (64 to 69): only up or down. */
    bool isSwitch (Controller);

    /** For showing a value: "+50%" (bend), "64" (0..127), "Down" / "Up" (a pedal), "L20" (pan). */
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
        there in between is replaced by small steps along it (one whenever the value changes; a
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
