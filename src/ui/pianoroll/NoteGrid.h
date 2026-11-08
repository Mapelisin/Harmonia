#pragma once

#include "ui/pianoroll/GridGestures.h"

namespace harmonia::ui::pianoroll
{
    /** The note area: rows, grid, ghost notes, notes, selection and the playhead. Only the
        part being repainted is drawn, so moving the playhead stays cheap even with many notes. */
    class NoteGrid final : public juce::Component
    {
    public:
        NoteGrid (PianoRollModel&, View&);

        void paint (juce::Graphics&) override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        GridGestures& getGestures() noexcept                { return gestures; }

        /** Moves the playhead line (song beats), or hides it. */
        void setPlayheadBeat (std::optional<double>);

        /** Zoom or scroll changed here (wheel, middle button). */
        std::function<void()> onViewChanged;

    private:
        PianoRollModel& model;
        View& view;
        GridGestures gestures { model, view };
        std::optional<double> playheadBeat;

        void paintRows (juce::Graphics&, juce::Rectangle<int> area);
        void paintGridLines (juce::Graphics&, juce::Rectangle<int> area);
        void paintClipAndTimeSelection (juce::Graphics&, juce::Rectangle<int> area);
        void paintGhostNotes (juce::Graphics&, juce::Rectangle<int> area);
        void paintNotes (juce::Graphics&, juce::Rectangle<int> area);
        void paintOverlays (juce::Graphics&);

        juce::Rectangle<int> getPlayheadArea() const;
        GridGestures::Pointer toPointer (const juce::MouseEvent&) const;
    };

    /** Finest grid line spacing to draw: the snap size when it is wide enough, otherwise the zoom's. */
    double getVisibleGridBeats (PianoRollModel&, const View&);
}
