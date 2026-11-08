#include "ui/automation/AutomationGestures.h"

namespace harmonia::ui::automation
{
    namespace automation = audio::automation;

    AutomationGestures::AutomationGestures (arrangement::ArrangementModel& m, const arrangement::ArrangementView& v)
        : model (m), view (v)
    {
    }

    void AutomationGestures::setParameter (te::AutomatableParameter* newParameter)
    {
        parameter = newParameter;
        dragged = -1;
        erasing = false;
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

    double AutomationGestures::toBeat (const Pointer& p) const
    {
        const auto beat = std::max (0.0, view.xToBeat (p.position.x));

        if (p.mods.isAltDown())
            return beat;

        return std::max (0.0, core::snap::snapNearest (beat, model.getSnap(), model.getSnapContext()));
    }

    void AutomationGestures::down (const Pointer& p)
    {
        dragged = -1;
        erasing = false;

        if (parameter == nullptr)
            return;

        auto& project = model.getProject();
        const auto hit = pointAt (p.position);

        if (p.mods.isPopupMenu())
        {
            erasing = true;
            project.beginGesture ("Delete automation points");
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

        if (dragged < 0)
            return;

        const auto beat = p.mods.isShiftDown() ? grabBeat : toBeat (p);
        automation::movePoint (*parameter, dragged, beat, yToValue (p.position.y));
    }

    void AutomationGestures::up (const Pointer&)
    {
        dragged = -1;
        erasing = false;
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

        return pointAt (position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor;
    }
}
