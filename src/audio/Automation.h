#pragma once

#include "core/Session.h"

/** Automation: a parameter of a track (volume, pan, a send, an effect's or the instrument's knob)
    following a line drawn over the song. The line is Tracktion's automation curve of the parameter;
    while the song plays (and in the export) the parameter follows it, between the points in straight
    lines, before the first point at its value and after the last at its.

    Positions are song beats, values 0..1 of the parameter's range (as its knob shows it). Every
    change goes into the current undo transaction: the caller starts it (one drag = one step). */
namespace harmonia::audio::automation
{
    struct Point
    {
        double beat = 0.0;
        float value = 0.0f;

        bool operator== (const Point&) const = default;
    };

    /** The track's parameters that can follow a line, in the order they are offered: volume, pan,
        the sends (to the return tracks they go to), then the effects' and the instrument's. */
    juce::Array<te::AutomatableParameter*> getParameters (te::AudioTrack&);

    /** "Volume", "Pan", "Send: Reverb", "Low-pass: Frequency"... */
    juce::String getDisplayName (te::AutomatableParameter&);

    /** The plugin's name for a menu ("EQ", "4OSC"...); empty for volume, pan and sends. */
    juce::String getGroupName (te::AutomatableParameter&);

    /** The parameter's value as its knob would show it ("-6.0 dB", "L20"...), for 0..1. */
    juce::String valueToText (te::AutomatableParameter&, float value);

    std::vector<Point> getPoints (te::AutomatableParameter&);
    bool hasPoints (const te::AutomatableParameter&);

    /** The track's parameters that have a line (to mark them in menus). */
    juce::Array<te::AutomatableParameter*> getAutomated (te::AudioTrack&);

    /** The 0..1 value at `beat`: from the line, or where the parameter's knob is without one. */
    float getValueAt (te::AutomatableParameter&, double beat);

    /** Adds a point (at least beat 0, value clamped to 0..1). Returns its index. */
    int addPoint (te::AutomatableParameter&, double beat, float value);

    /** Moves a point: not past its neighbours (it keeps its index), value clamped. */
    void movePoint (te::AutomatableParameter&, int index, double beat, float value);

    void removePoint (te::AutomatableParameter&, int index);

    /** Removes the whole line: the parameter goes back to where its knob is. (So does removing
        the last point.) */
    void clear (te::AutomatableParameter&);
}
