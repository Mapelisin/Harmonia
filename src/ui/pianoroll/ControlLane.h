#pragma once

#include "ui/pianoroll/PianoRollModel.h"
#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    /** The lower panel: one stem per note showing velocity (or another per-note value).
        Swipe with the left button to set the values of every note crossed; Alt resets them.
        With a selection only the selected notes change, which is how one note of a chord is edited. */
    class ControlLane final : public juce::Component
    {
    public:
        ControlLane (PianoRollModel&, View&);

        void paint (juce::Graphics&) override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        PianoRollModel& model;
        View& view;
        double lastBeat = 0.0;
        float lastValue = 0.0f;
        bool editing = false;
        juce::String hoverText;

        float valueAt (const juce::MouseEvent&) const;
        float valueToY (float value) const;
        void applySwipe (const juce::MouseEvent&);
        void updateHoverText (const juce::MouseEvent&);
    };
}
