#include "ui/automation/AutomationGestures.h"

namespace harmonia::ui::automation
{
    namespace automation = audio::automation;

    namespace
    {
        /** The points last copied, for every lane. */
        std::vector<automation::Point>& copiedPoints()
        {
            static std::vector<automation::Point> points;
            return points;
        }
    }

    AutomationGestures::AutomationGestures (arrangement::ArrangementModel& m, const arrangement::ArrangementView& v)
        : model (m), view (v)
    {
    }

    void AutomationGestures::setParameter (te::AutomatableParameter* newParameter)
    {
        parameter = newParameter;
        dragged = bending = -1;
        erasing = selecting = movingSelection = false;
        selection.clear();
    }

    float AutomationGestures::valueToY (float value) const noexcept
    {
        return margin + (1.0f - value) * std::max (1.0f, height - 2.0f * margin);
    }

    float AutomationGestures::yToValue (float y) const noexcept
    {
        return juce::jlimit (0.0f, 1.0f, 1.0f - (y - margin) / std::max (1.0f, height - 2.0f * margin));
    }

    int AutomationGestures::pointAt (juce::Point<float> position) const
    {
        if (parameter == nullptr)
            return -1;

        const auto points = automation::getPoints (*parameter);
        auto best = -1;
        auto bestDistance = hitRadius + 1.0f;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const juce::Point<float> at { view.beatToX (points[i].beat), valueToY (points[i].value) };
            const auto distance = at.getDistanceFrom (position);

            if (distance <= hitRadius && distance < bestDistance)
            {
                best = (int) i;
                bestDistance = distance;
            }
        }

        return best;
    }

    std::optional<juce::Point<float>> AutomationGestures::getBendHandle (int index) const
    {
        if (parameter == nullptr)
            return std::nullopt;

        const auto handle = automation::getBendHandle (*parameter, index);

        if (! handle.has_value())
            return std::nullopt;

        const auto points = automation::getPoints (*parameter);

        if (view.beatToX (points[(size_t) index + 1].beat) - view.beatToX (points[(size_t) index].beat) < minBendPixels)
            return std::nullopt;

        return juce::Point<float> { view.beatToX (handle->beat), valueToY (handle->value) };
    }

    int AutomationGestures::bendAt (juce::Point<float> position) const
    {
        if (parameter == nullptr)
            return -1;

        auto best = -1;
        auto bestDistance = bendHitRadius + 1.0f;

        for (int i = 0; i + 1 < parameter->getCurve().getNumPoints(); ++i)
        {
            if (const auto handle = getBendHandle (i))
            {
                if (const auto distance = handle->getDistanceFrom (position); distance <= bendHitRadius && distance < bestDistance)
                {
                    best = i;
                    bestDistance = distance;
                }
            }
        }

        return best;
    }

    //==============================================================================
    std::vector<int> AutomationGestures::getSelection() const
    {
        if (parameter == nullptr || selection.empty() || automation::getPoints (*parameter) != selectedLine)
            return {};

        return selection;
    }

    bool AutomationGestures::isSelected (int index) const
    {
        const auto selected = getSelection();
        return std::binary_search (selected.begin(), selected.end(), index);
    }

    void AutomationGestures::setSelection (std::vector<int> indices)
    {
        selection.clear();
        selectedLine.clear();

        if (parameter == nullptr)
            return;

        selectedLine = automation::getPoints (*parameter);
        std::sort (indices.begin(), indices.end());
        indices.erase (std::unique (indices.begin(), indices.end()), indices.end());

        for (const auto index : indices)
            if (juce::isPositiveAndBelow (index, (int) selectedLine.size()))
                selection.push_back (index);
    }

    void AutomationGestures::selectAll()
    {
        if (parameter == nullptr)
            return;

        std::vector<int> all ((size_t) parameter->getCurve().getNumPoints());
        std::iota (all.begin(), all.end(), 0);
        setSelection (std::move (all));
    }

    std::optional<juce::Rectangle<float>> AutomationGestures::getRectangle() const
    {
        if (! selecting)
            return std::nullopt;

        return juce::Rectangle<float> (rectangleStart, rectangleEnd);
    }

    void AutomationGestures::selectInRectangle()
    {
        const auto rectangle = juce::Rectangle<float> (rectangleStart, rectangleEnd);
        const auto points = automation::getPoints (*parameter);
        auto selected = selectionAtStart;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const juce::Point<float> at { view.beatToX (points[i].beat), valueToY (points[i].value) };

            // Edges included: a rectangle drawn flat along the line's level takes its points.
            if (at.x >= rectangle.getX() && at.x <= rectangle.getRight() && at.y >= rectangle.getY() && at.y <= rectangle.getBottom())
                selected.push_back ((int) i);
        }

        setSelection (std::move (selected));
    }

    bool AutomationGestures::deleteSelected()
    {
        const auto selected = getSelection();

        if (selected.empty())
            return false;

        model.getProject().beginGesture (selected.size() == 1 ? "Delete automation point" : "Delete automation points");
        automation::removePoints (*parameter, selected);
        clearSelection();
        return true;
    }

    bool AutomationGestures::copySelected()
    {
        const auto selected = getSelection();

        if (selected.empty())
            return false;

        copiedPoints() = automation::copyPoints (*parameter, selected);
        return true;
    }

    bool AutomationGestures::cutSelected()
    {
        const auto selected = getSelection();

        if (! copySelected())
            return false;

        model.getProject().beginGesture ("Cut automation points");
        automation::removePoints (*parameter, selected);
        clearSelection();
        return true;
    }

    bool AutomationGestures::canPaste()
    {
        return ! copiedPoints().empty();
    }

    bool AutomationGestures::paste (double beat)
    {
        if (parameter == nullptr || ! canPaste())
            return false;

        model.getProject().beginGesture ("Paste automation points");
        setSelection (automation::pastePoints (*parameter, copiedPoints(), beat));
        return true;
    }

    //==============================================================================
    double AutomationGestures::toBeat (const Pointer& p) const
    {
        const auto beat = std::max (0.0, view.xToBeat (p.position.x));

        if (p.mods.isAltDown())
            return beat;

        return std::max (0.0, core::snap::snapNearest (beat, model.getSnap(), model.getSnapContext()));
    }

    void AutomationGestures::down (const Pointer& p)
    {
        dragged = bending = -1;
        erasing = selecting = movingSelection = false;

        if (parameter == nullptr)
            return;

        auto& project = model.getProject();
        const auto hit = pointAt (p.position);
        const auto bend = hit >= 0 ? -1 : bendAt (p.position);       // a point is taken before a handle

        // Ctrl: what is selected. Nothing of the line changes.
        if (p.mods.isCommandDown() && ! p.mods.isPopupMenu())
        {
            auto selected = getSelection();

            if (hit >= 0)
            {
                if (const auto found = std::find (selected.begin(), selected.end(), hit); found != selected.end())
                    selected.erase (found);
                else
                    selected.push_back (hit);

                setSelection (std::move (selected));
                return;
            }

            selecting = true;
            rectangleStart = rectangleEnd = p.position;
            selectionAtStart = p.mods.isShiftDown() ? selected : std::vector<int>();
            setSelection (selectionAtStart);
            return;
        }

        // One of several selected points: they all go with it.
        if (hit >= 0 && p.clicks < 2 && ! p.mods.isPopupMenu() && isSelected (hit) && getSelection().size() > 1)
        {
            project.beginGesture ("Move automation points");
            movingSelection = true;
            dragged = hit;
            moveFrom = automation::getPoints (*parameter);
            grabBeat = moveFrom[(size_t) hit].beat;
            grabValue = yToValue (p.position.y);
            return;
        }

        clearSelection();

        if (p.mods.isPopupMenu())
        {
            erasing = true;
            project.beginGesture (bend >= 0 ? "Straighten automation line" : "Delete automation points");

            if (bend >= 0)
                automation::setCurve (*parameter, bend, 0.0f);

            eraseAt (p.position);
            return;
        }

        if (hit >= 0 && p.clicks >= 2)
        {
            project.beginGesture ("Delete automation point");
            automation::removePoint (*parameter, hit);
            return;
        }

        if (hit >= 0)
        {
            project.beginGesture ("Move automation point");
            dragged = hit;
            grabBeat = automation::getPoints (*parameter)[(size_t) hit].beat;
            return;
        }

        if (bend >= 0 && p.clicks >= 2)
        {
            project.beginGesture ("Straighten automation line");
            automation::setCurve (*parameter, bend, 0.0f);
            return;
        }

        if (bend >= 0)
        {
            project.beginGesture ("Bend automation line");
            bending = bend;
            bendGrabOffset = getBendHandle (bend)->y - p.position.y;
            return;
        }

        project.beginGesture ("Add automation point");
        dragged = automation::addPoint (*parameter, toBeat (p), yToValue (p.position.y));
        grabBeat = toBeat (p);
    }

    void AutomationGestures::drag (const Pointer& p)
    {
        if (parameter == nullptr)
            return;

        if (erasing)
        {
            eraseAt (p.position);
            return;
        }

        if (selecting)
        {
            rectangleEnd = p.position;
            selectInRectangle();
            return;
        }

        if (movingSelection)
        {
            const auto beat = p.mods.isShiftDown() ? grabBeat : toBeat (p);
            automation::movePoints (*parameter, selection, moveFrom, beat - grabBeat, yToValue (p.position.y) - grabValue);
            selectedLine = automation::getPoints (*parameter);       // the same points, moved
            return;
        }

        if (bending >= 0)
        {
            automation::setCurve (*parameter, bending,
                                  automation::getCurveForHandle (*parameter, bending, yToValue (p.position.y + bendGrabOffset)));
            return;
        }

        if (dragged < 0)
            return;

        const auto beat = p.mods.isShiftDown() ? grabBeat : toBeat (p);
        automation::movePoint (*parameter, dragged, beat, yToValue (p.position.y));
    }

    void AutomationGestures::up (const Pointer&)
    {
        dragged = bending = -1;
        erasing = selecting = movingSelection = false;
    }

    void AutomationGestures::eraseAt (juce::Point<float> position)
    {
        for (auto index = pointAt (position); index >= 0; index = pointAt (position))
            automation::removePoint (*parameter, index);
    }

    juce::MouseCursor AutomationGestures::getCursorFor (juce::Point<float> position) const
    {
        if (dragged >= 0)
            return juce::MouseCursor::DraggingHandCursor;

        if (bending >= 0)
            return juce::MouseCursor::UpDownResizeCursor;

        if (pointAt (position) >= 0)
            return juce::MouseCursor::DraggingHandCursor;

        return bendAt (position) >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor;
    }
}
