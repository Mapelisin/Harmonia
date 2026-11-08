#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::arrangement
{
    /** Bar numbers above the clips. Click: play from there (song mode). Ctrl+drag: the loop
        region; Ctrl+click: no loop region. In clip mode the open clip's range is marked. Right
        button: markers, and bars on every track. */
    class ArrangementRuler final : public juce::Component
    {
    public:
        ArrangementRuler (ArrangementModel&, ArrangementView&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        void mouseDoubleClick (const juce::MouseEvent&) override;

        void setPlayheadBeat (std::optional<double>);

        /** A song position was clicked. */
        std::function<void (double beat)> onPositionChosen;

        /** The user wants to rename this marker (the owner asks for the name). */
        std::function<void (const core::markers::Marker&)> onRenameMarker;

        /** The strip at the top where the markers are. */
        static constexpr float markerLaneHeight = 18.0f;

        /** The marker whose flag is at this point (ruler coordinates), if any. */
        std::optional<core::markers::Marker> findMarkerAt (juce::Point<float>) const;

        /** The right-click menu: add a marker at `beat`; rename, loop or delete `marker`;
            duplicate, insert or delete bars on every track: the loop region if `clickedBeat`
            (where the mouse was, not snapped; `beat` if none) is in it, else the bar there. */
        juce::PopupMenu createMenu (double beat, std::optional<core::markers::Marker> marker,
                                    std::optional<double> clickedBeat = std::nullopt);

    private:
        ArrangementModel& model;
        ArrangementView& view;
        std::optional<double> playheadBeat;

        bool settingLoop = false;
        double loopAnchor = 0.0;

        // A marker being dragged: its tree, and how far from the mouse it is.
        std::optional<core::markers::Marker> draggedMarker;
        double dragOffsetBeats = 0.0;
        bool markerMoved = false;

        juce::Rectangle<float> getFlag (const core::markers::Marker&, juce::Graphics*) const;
        void paintMarkers (juce::Graphics&);

        double snapped (const juce::MouseEvent&) const;
        void addBarItems (juce::PopupMenu&, double clickedBeat);
    };
}
