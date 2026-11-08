#pragma once

#include "ui/automation/AutomationGestures.h"

namespace harmonia::ui::automation
{
    /** The line of one parameter over the song, under the arrangement and lined up with it (the
        same beats across). Draws the bars, the line with its points and the playhead; the mouse
        edits it (see AutomationGestures). */
    class AutomationLane final : public juce::Component
    {
    public:
        AutomationLane (arrangement::ArrangementModel&, const arrangement::ArrangementView&);

        AutomationGestures& getGestures() noexcept          { return gestures; }

        void setParameter (te::AutomatableParameter*);
        te::AutomatableParameter* getParameter() const noexcept { return gestures.getParameter(); }

        /** Colour of the line (the track's). */
        void setLineColour (juce::Colour);

        void setPlayheadBeat (std::optional<double>);

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        /** A point was added, moved or deleted by the mouse. */
        std::function<void()> onEdited;

    private:
        arrangement::ArrangementModel& model;
        const arrangement::ArrangementView& view;
        AutomationGestures gestures { model, view };
        juce::Colour lineColour { 0xff4f9fe0 };
        std::optional<double> playheadBeat;
        int hovered = -1;

        void paintGrid (juce::Graphics&);
        void paintLine (juce::Graphics&, te::AutomatableParameter&);
        void paintReadout (juce::Graphics&, te::AutomatableParameter&, int pointIndex);
        void updateHover (juce::Point<float>);
        AutomationGestures::Pointer toPointer (const juce::MouseEvent&) const;
    };
}
