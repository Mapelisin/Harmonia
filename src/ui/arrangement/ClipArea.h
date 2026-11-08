#pragma once

#include "ui/arrangement/ArrangementGestures.h"

namespace harmonia::ui::arrangement
{
    /** The tracks' rows with their clips (a small picture of each clip's notes), the grid, the
        loop region and the playhead. Mouse input goes to ArrangementGestures. */
    class ClipArea final : public juce::Component
    {
    public:
        ClipArea (ArrangementModel&, ArrangementView&);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        ArrangementGestures& getGestures() noexcept     { return gestures; }

        /** Moves the playhead line, repainting only the strips it leaves and enters. */
        void setPlayheadBeat (std::optional<double>);

        /** The view was zoomed or scrolled with the mouse. */
        std::function<void()> onViewChanged;

    private:
        ArrangementModel& model;
        ArrangementView& view;
        ArrangementGestures gestures { model, view };
        std::optional<double> playheadBeat;

        void paintGrid (juce::Graphics&, juce::Rectangle<int> clip);
        void paintClip (juce::Graphics&, const DrawnClip&);
        void repaintPlayheadAt (std::optional<double> beat);

        static ArrangementGestures::Pointer toPointer (const juce::MouseEvent&);
    };
}
