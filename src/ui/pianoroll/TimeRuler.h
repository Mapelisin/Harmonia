#pragma once

#include "ui/pianoroll/PianoRollModel.h"
#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    /** The song's markers, bar numbers above the notes, the clip's extent and the playhead.
        Click or drag: move the playhead. Ctrl+drag or double-click and drag: select a time range
        (also selects the notes that start in it); Ctrl+click clears it. A click on a marker plays
        from it. A looping clip's loop end is a handle: drag it to change the loop's length. */
    class TimeRuler final : public juce::Component
    {
    public:
        TimeRuler (PianoRollModel&, View&);

        void paint (juce::Graphics&) override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        static constexpr float markerLaneHeight = 14.0f;

        /** The marker whose flag is at `position`, if any. */
        std::optional<core::markers::Marker> findMarkerAt (juce::Point<float> position) const;

        /** Where a looping clip's loop-end handle is (x), if the clip loops. */
        std::optional<float> getLoopEndX() const;

        void setPlayheadBeat (std::optional<double>);

        /** The user picked a playback position (song beats). */
        std::function<void (double)> onPositionChosen;

    private:
        PianoRollModel& model;
        View& view;
        std::optional<double> playheadBeat;
        bool selectingTime = false, draggingLoopEnd = false;
        double selectionAnchor = 0.0, loopStartBeat = 0.0;

        double snapped (const juce::MouseEvent&) const;
        bool isOverLoopEnd (juce::Point<float>) const;
        juce::Rectangle<float> getFlag (const core::markers::Marker&) const;
        void paintMarkers (juce::Graphics&);
        void paintClipStrip (juce::Graphics&, juce::Rectangle<float> bounds);
        juce::Rectangle<int> getPlayheadArea() const;
    };
}
