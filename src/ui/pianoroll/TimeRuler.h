#pragma once

#include "ui/pianoroll/PianoRollModel.h"
#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    /** Bar numbers above the notes, the clip's extent and the playhead.
        Click or drag: move the playhead. Ctrl+drag or double-click and drag: select a time range
        (also selects the notes that start in it); Ctrl+click clears it. */
    class TimeRuler final : public juce::Component
    {
    public:
        TimeRuler (PianoRollModel&, View&);

        void paint (juce::Graphics&) override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        void setPlayheadBeat (std::optional<double>);

        /** The user picked a playback position (song beats). */
        std::function<void (double)> onPositionChosen;

    private:
        PianoRollModel& model;
        View& view;
        std::optional<double> playheadBeat;
        bool selectingTime = false;
        double selectionAnchor = 0.0;

        double snapped (const juce::MouseEvent&) const;
        juce::Rectangle<int> getPlayheadArea() const;
    };
}
