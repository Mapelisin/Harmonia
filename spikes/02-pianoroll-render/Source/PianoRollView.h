#pragma once

#include "NoteSet.h"

namespace harmonia::spike
{
    /** Draws a piano roll: keyboard, key rows, bar/beat/step grid and notes.
        Records how long each paint() call takes. */
    class PianoRollView : public juce::Component
    {
    public:
        static constexpr int keyboardWidth = 64;

        void setNotes (const NoteSet*);

        /** firstBeat: beat at the left edge; pixelsPerBeat: horizontal zoom;
            topPitch: pitch row at the top edge; rowHeight: vertical zoom. */
        void setView (double firstBeat, double pixelsPerBeat, double topPitch, float rowHeight);

        double getFirstBeat() const noexcept       { return firstBeat; }
        double getPixelsPerBeat() const noexcept   { return pixelsPerBeat; }

        void setShowLabels (bool shouldShow)       { showLabels = shouldShow; repaint(); }

        /** Paint durations in milliseconds and the timestamps (ms) when each paint started. */
        std::vector<double> paintDurationsMs, paintStartTimesMs;
        size_t lastVisibleNoteCount = 0;

        void paint (juce::Graphics&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        const NoteSet* notes = nullptr;
        double firstBeat = 0.0, pixelsPerBeat = 48.0, topPitch = 84.0;
        float rowHeight = 14.0f;
        bool showLabels = true;
        juce::Point<double> dragStartView;

        float beatToX (double beat) const noexcept   { return (float) (keyboardWidth + (beat - firstBeat) * pixelsPerBeat); }
        float pitchToY (int pitch) const noexcept    { return (float) ((topPitch - pitch) * rowHeight); }

        void paintRows (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintGrid (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintNotes (juce::Graphics&, juce::Rectangle<float> area);
        void paintKeyboard (juce::Graphics&) const;
    };
}
