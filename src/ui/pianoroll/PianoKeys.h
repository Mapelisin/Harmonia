#pragma once

#include "ui/pianoroll/PianoRollModel.h"
#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    /** The keyboard left of the notes. Clicking a key plays it (further right = louder);
        the middle button zooms vertically; the wheel scrolls (Ctrl+wheel zooms). With drum
        names on, each key says which drum it plays. Right button: its menu (drum names). */
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

        /** The right button's menu. */
        juce::PopupMenu createMenu();

    private:
        PianoRollModel& model;
        View& view;
        int pressedPitch = -1;
        float lastZoomY = 0.0f;

        void press (const juce::MouseEvent&);
        void viewChanged();
    };
}
