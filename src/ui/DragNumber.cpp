#include "ui/DragNumber.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    DragNumber::DragNumber()
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus (false);
    }

    DragNumber::~DragNumber()
    {
        if (editor != nullptr)
            editor->removeListener (this);
    }

    void DragNumber::setRange (double newMinimum, double newMaximum)
    {
        minimum = newMinimum;
        maximum = std::max (newMinimum, newMaximum);
        value = juce::jlimit (minimum, maximum, value);
        repaint();
    }

    void DragNumber::setSteps (double coarse, double fine, float pixels)
    {
        coarseStep = coarse;
        fineStep = fine;
        pixelsPerStep = std::max (1.0f, pixels);
    }

    void DragNumber::setValue (double newValue)
    {
        if (isBeingEdited())
            return;

        newValue = juce::jlimit (minimum, maximum, newValue);

        if (newValue != value)
        {
            value = newValue;
            repaint();
        }
    }

    juce::String DragNumber::getText() const
    {
        return textFromValue != nullptr ? textFromValue (value) : juce::String (value);
    }

    void DragNumber::change (double newValue)
    {
        newValue = juce::jlimit (minimum, maximum, newValue);

        if (newValue == value)
            return;

        value = newValue;
        repaint();

        if (onValueChange != nullptr)
            onValueChange (value);
    }

    //==============================================================================
    void DragNumber::beginDrag (float y)
    {
        closeEditor (true);
        dragging = true;
        dragStartY = y;
        dragStartValue = value;

        if (onGestureStart != nullptr)
            onGestureStart();
    }

    void DragNumber::dragTo (float y, bool fine)
    {
        if (! dragging)
            return;

        const auto step = fine ? fineStep : coarseStep;
        const auto steps = std::trunc ((dragStartY - y) / pixelsPerStep);
        auto newValue = dragStartValue + steps * step;

        if (step > 0.0)
            newValue = std::round (newValue / step) * step;

        change (newValue);
    }

    void DragNumber::endDrag()
    {
        if (! dragging)
            return;

        dragging = false;

        if (onGestureEnd != nullptr)
            onGestureEnd();
    }

    void DragNumber::turnWheel (int notches)
    {
        if (notches == 0)
            return;

        if (onGestureStart != nullptr)
            onGestureStart();

        auto newValue = value + notches * coarseStep;

        if (coarseStep > 0.0)
            newValue = std::round (newValue / coarseStep) * coarseStep;

        change (newValue);

        if (onGestureEnd != nullptr)
            onGestureEnd();
    }

    //==============================================================================
    void DragNumber::showEditor()
    {
        if (editor != nullptr)
            return;

        editor = std::make_unique<juce::TextEditor>();
        editor->setFont (juce::FontOptions (theme::size::fontSize));
        editor->setJustification (juce::Justification::centred);
        editor->setInputRestrictions (8, "0123456789.,");
        editor->setText (juce::String (value), false);
        editor->setColour (juce::TextEditor::backgroundColourId, theme::colour::background);
        editor->setColour (juce::TextEditor::textColourId, theme::colour::textPrimary);
        editor->setColour (juce::TextEditor::outlineColourId, theme::colour::accent);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, theme::colour::accent);
        editor->addListener (this);
        addAndMakeVisible (*editor);
        resized();
        editor->selectAll();

        if (isShowing())
            editor->grabKeyboardFocus();

        repaint();
    }

    void DragNumber::typeValue (const juce::String& text)
    {
        showEditor();
        editor->setText (text, false);
        textEditorReturnKeyPressed (*editor);
    }

    void DragNumber::closeEditor (bool commit)
    {
        if (editor == nullptr)
            return;

        // Taken out first: committing can make the parent repaint or move the focus.
        auto closing = std::move (editor);
        closing->removeListener (this);
        const auto text = closing->getText().replaceCharacter (',', '.').trim();
        removeChildComponent (closing.get());

        if (commit && text.containsAnyOf ("0123456789"))
        {
            const auto typed = juce::jlimit (minimum, maximum, text.getDoubleValue());

            if (typed != value)
            {
                if (onGestureStart != nullptr)
                    onGestureStart();

                change (typed);

                if (onGestureEnd != nullptr)
                    onGestureEnd();
            }
        }

        repaint();
    }

    void DragNumber::textEditorReturnKeyPressed (juce::TextEditor&)
    {
        closeEditor (true);
    }

    void DragNumber::textEditorEscapeKeyPressed (juce::TextEditor&)
    {
        closeEditor (false);
    }

    void DragNumber::textEditorFocusLost (juce::TextEditor&)
    {
        // Clicking elsewhere keeps what was typed, as in a text field. Not right away: the
        // editor is still inside its own callback.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DragNumber> (this)]
        {
            if (safe != nullptr)
                safe->closeEditor (true);
        });
    }

    //==============================================================================
    void DragNumber::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (dragging ? theme::colour::surfaceRaised.brighter (0.1f) : theme::colour::surfaceRaised);
        g.fillRoundedRectangle (bounds, theme::size::cornerRadius);
        g.setColour (dragging ? theme::colour::accent : theme::colour::border);
        g.drawRoundedRectangle (bounds, theme::size::cornerRadius, 1.0f);

        if (editor == nullptr)
        {
            // Disabled (nothing it could change now): greyed, like a disabled button.
            g.setColour (isEnabled() ? theme::colour::textPrimary : theme::colour::textSecondary.withAlpha (0.6f));
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText (getText(), getLocalBounds().reduced (4, 0), juce::Justification::centred, true);
        }
    }

    void DragNumber::enablementChanged()
    {
        repaint();
    }

    void DragNumber::resized()
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds().reduced (1));
    }

    void DragNumber::mouseDown (const juce::MouseEvent& e)
    {
        beginDrag (e.position.y);
    }

    void DragNumber::mouseDrag (const juce::MouseEvent& e)
    {
        dragTo (e.position.y, e.mods.isShiftDown());
    }

    void DragNumber::mouseUp (const juce::MouseEvent&)
    {
        endDrag();
    }

    void DragNumber::mouseDoubleClick (const juce::MouseEvent&)
    {
        endDrag();
        showEditor();
    }

    void DragNumber::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        // Wheels and touchpads send fractions of a notch: add them up.
        wheelRemainder += (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 4.0f;
        const auto notches = (int) std::trunc (wheelRemainder);
        wheelRemainder -= (float) notches;
        turnWheel (notches);
    }
}
