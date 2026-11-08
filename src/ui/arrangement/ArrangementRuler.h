#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::arrangement
{
    /** Bar numbers above the clips. Click: play from there (song mode). Ctrl+drag: the loop
        region; Ctrl+click: no loop region. In clip mode the open clip's range is marked. */
    class ArrangementRuler final : public juce::Component
    {
    public:
        ArrangementRuler (ArrangementModel&, ArrangementView&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        void setPlayheadBeat (std::optional<double>);

        /** A song position was clicked. */
        std::function<void (double beat)> onPositionChosen;

    private:
        ArrangementModel& model;
        ArrangementView& view;
        std::optional<double> playheadBeat;

        bool settingLoop = false;
        double loopAnchor = 0.0;

        double snapped (const juce::MouseEvent&) const;
    };
}
