#pragma once

#include "ui/pianoroll/PianoRollModel.h"
#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    /** The keyboard left of the notes. Clicking a key plays it (further right = louder);
        the middle button zooms vertically; the wheel scrolls (Ctrl+wheel zooms). */
    class PianoKeys final : public juce::Component
    {
    public:
        PianoKeys (PianoRollModel&, View&);

        void paint (juce::Graphics&) override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        std::function<void()> onViewChanged;

    private:
        PianoRollModel& model;
        View& view;
        int pressedPitch = -1;
        float lastZoomY = 0.0f;

        void press (const juce::MouseEvent&);
        void viewChanged();
    };
}
