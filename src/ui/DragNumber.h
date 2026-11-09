#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui
{
    /** A number shown as text that the mouse changes, like FL Studio's tempo: drag up / down
        (Shift: finer), turn the wheel, or double-click and type it (Enter: set, Escape: never mind).

        Every drag, wheel turn or typed value is one gesture: onGestureStart, then onValueChange
        for each new value, then onGestureEnd (for one undo step). */
    class DragNumber final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::TextEditor::Listener
    {
    public:
        DragNumber();
        ~DragNumber() override;

        void setRange (double minimum, double maximum);

        /** How much one step of the drag (`pixelsPerStep` pixels) or a wheel notch changes the value,
            and the finer step with Shift. Dragged values are rounded to the step. */
        void setSteps (double coarse, double fine, float pixelsPerStep = 3.0f);

        /** Shows a value (from the model). Ignored while the user drags or types. */
        void setValue (double);
        double getValue() const noexcept                    { return value; }

        /** Dragging or typing right now. */
        bool isBeingEdited() const noexcept                 { return dragging || editor != nullptr; }

        std::function<juce::String (double)> textFromValue;
        std::function<void()> onGestureStart;
        std::function<void (double)> onValueChange;
        std::function<void()> onGestureEnd;

        //==============================================================================
        // What the mouse and the keyboard do (public for tests).

        void beginDrag (float y);
        void dragTo (float y, bool fine);
        void endDrag();
        void turnWheel (int notches);

        void showEditor();
        juce::TextEditor* getEditor() const noexcept        { return editor.get(); }

        /** Types `text` into the editor (opened if needed) and presses Enter. */
        void typeValue (const juce::String& text);

        //==============================================================================
        void paint (juce::Graphics&) override;
        void resized() override;
        void enablementChanged() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        double value = 0.0, minimum = 0.0, maximum = 1.0;
        double coarseStep = 1.0, fineStep = 0.1;
        float pixelsPerStep = 3.0f;

        bool dragging = false;
        float dragStartY = 0.0f;
        double dragStartValue = 0.0;
        float wheelRemainder = 0.0f;

        std::unique_ptr<juce::TextEditor> editor;

        juce::String getText() const;
        void change (double newValue);
        void closeEditor (bool commit);

        void textEditorReturnKeyPressed (juce::TextEditor&) override;
        void textEditorEscapeKeyPressed (juce::TextEditor&) override;
        void textEditorFocusLost (juce::TextEditor&) override;

        JUCE_DECLARE_NON_COPYABLE (DragNumber)
    };
}
