#pragma once

#include "core/Session.h"

/** Automation: a parameter of a track (volume, pan, a send, an effect's or the instrument's knob)
    following a line drawn over the song. The line is Tracktion's automation curve of the parameter;
    while the song plays (and in the export) the parameter follows it, between the points in straight
    lines or bent ones, before the first point at its value and after the last at its.

    Positions are song beats, values 0..1 of the parameter's range (as its knob shows it). Every
    change goes into the current undo transaction: the caller starts it (one drag = one step). */
namespace harmonia::audio::automation
{
    struct Point
    {
        double beat = 0.0;
        float value = 0.0f;

        /** How the line from this point to the next bends: 0 is straight; towards 1 the value
            changes later (it holds, then goes), towards -1 sooner. */
        float curve = 0.0f;

        bool operator== (const Point&) const = default;
    };

    /** The track's parameters that can follow a line, in the order they are offered: volume, pan,
        the sends (to the return tracks they go to), then the effects' and the instrument's. */
    juce::Array<te::AutomatableParameter*> getParameters (te::AudioTrack&);

    /** The master's: its volume and pan, then its effects'. */
    juce::Array<te::AutomatableParameter*> getMasterParameters (te::Edit&);

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

    /** Adds a point (at least beat 0, value clamped to 0..1). Returns its index. Put on a bent
        stretch of the line, it bends on as the stretch did. */
    int addPoint (te::AutomatableParameter&, double beat, float value);

    /** Moves a point: not past its neighbours (it keeps its index), value clamped. */
    void movePoint (te::AutomatableParameter&, int index, double beat, float value);

    void removePoint (te::AutomatableParameter&, int index);

    /** Moves several points together, as one shape: each `beatShift` later and `valueShift` (of
        0..1) higher than it is in `from` (the line's points when the move began; `indices` are
        into them). The shift is cut short where a point would pass a point that stays, or beat
        0, or leave 0..1: all of them move by the same, or none further. */
    void movePoints (te::AutomatableParameter&, const std::vector<int>& indices, const std::vector<Point>& from,
                     double beatShift, float valueShift);

    void removePoints (te::AutomatableParameter&, std::vector<int> indices);

    /** Points of the line as a shape to put elsewhere: in time order, their beats counted from
        the first of them. */
    std::vector<Point> copyPoints (te::AutomatableParameter&, std::vector<int> indices);

    /** Puts a copied shape on the line, its first point at `beat`, in place of the points the
        line had from there to the shape's end. Returns the indices of the points put. */
    std::vector<int> pastePoints (te::AutomatableParameter&, const std::vector<Point>& shape, double beat);

    /** Bends the line between point `index` and the next (-1..1, see Point::curve). */
    void setCurve (te::AutomatableParameter&, int index, float curve);

    /** Where the line between point `index` and the next is taken to bend it: the middle of its
        bend (`curve` is the bend it has). Nothing if it can't bend: after the last point, in a
        jump, or where the two points have the same value. */
    std::optional<Point> getBendHandle (te::AutomatableParameter&, int index);

    /** The bend that puts that handle at `value` (0..1): what dragging it there asks for. */
    float getCurveForHandle (te::AutomatableParameter&, int index, float value);

    /** Removes the whole line: the parameter goes back to where its knob is. (So does removing
        the last point.) */
    void clear (te::AutomatableParameter&);
}
