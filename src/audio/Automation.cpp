#include "audio/Automation.h"
#include "audio/Effects.h"

namespace harmonia::audio::automation
{
    namespace
    {
        /** Not a knob of its own: the fader end and a return track's input. */
        bool isMixerPlugin (te::Plugin& plugin)
        {
            return dynamic_cast<te::VolumeAndPanPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::LevelMeterPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::VCAPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::AuxSendPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::AuxReturnPlugin*> (&plugin) != nullptr;
        }

        juce::UndoManager* getUndoManager (te::AutomatableParameter& parameter)
        {
            return &parameter.getEdit().getUndoManager();
        }

        te::EditPosition toCurvePosition (const te::AutomationCurve& curve, double beat)
        {
            const auto position = te::BeatPosition::fromBeats (std::max (0.0, beat));

            if (curve.timeBase == te::AutomationCurve::TimeBase::beats)
                return position;

            return curve.edit.tempoSequence.toTime (position);
        }

        double toBeat (const te::AutomationCurve& curve, te::EditPosition position)
        {
            return te::toBeats (position, curve.edit.tempoSequence).inBeats();
        }

        float toValue (te::AutomatableParameter& parameter, float value01)
        {
            return parameter.valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value01));
        }

        float to01 (te::AutomatableParameter& parameter, float value)
        {
            return juce::jlimit (0.0f, 1.0f, parameter.valueRange.convertTo0to1 (value));
        }

        /** Once the line is gone the parameter goes back to where its knob is (the line only
            overrode it). */
        void followKnobIfNoLine (te::AutomatableParameter& parameter)
        {
            if (! hasPoints (parameter))
                parameter.updateToFollowCurve (te::TimePosition());
        }
    }

    juce::Array<te::AutomatableParameter*> getParameters (te::AudioTrack& track)
    {
        juce::Array<te::AutomatableParameter*> result;

        if (auto* volume = track.getVolumePlugin())
        {
            result.add (volume->volParam.get());
            result.add (volume->panParam.get());
        }

        for (auto* plugin : track.pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
                result.add (send->gain.get());

        for (auto* plugin : track.pluginList)
            if (! isMixerPlugin (*plugin))
                for (auto* parameter : plugin->getAutomatableParameters())
                    result.add (parameter);

        return result;
    }

    juce::Array<te::AutomatableParameter*> getMasterParameters (te::Edit& edit)
    {
        juce::Array<te::AutomatableParameter*> result;

        if (const auto volume = edit.getMasterVolumePlugin())
        {
            result.add (volume->volParam.get());
            result.add (volume->panParam.get());
        }

        for (auto* plugin : edit.getMasterPluginList())
            if (! isMixerPlugin (*plugin))
                for (auto* parameter : plugin->getAutomatableParameters())
                    result.add (parameter);

        return result;
    }

    juce::String getDisplayName (te::AutomatableParameter& parameter)
    {
        auto* plugin = parameter.getPlugin();

        if (auto* volume = dynamic_cast<te::VolumeAndPanPlugin*> (plugin))
            return &parameter == volume->panParam.get() ? "Pan" : "Volume";

        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin))
            return "Send: " + send->getBusName();

        if (plugin != nullptr)
            return getGroupName (parameter) + ": " + parameter.getParameterName();

        return parameter.getParameterName();
    }

    juce::String getGroupName (te::AutomatableParameter& parameter)
    {
        auto* plugin = parameter.getPlugin();

        if (plugin == nullptr || dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr
            || dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr)
            return {};

        return effects::getDisplayName (*plugin);
    }

    juce::String valueToText (te::AutomatableParameter& parameter, float value01)
    {
        const auto text = parameter.valueToString (toValue (parameter, value01));
        const auto label = parameter.getLabel();
        return (label.isEmpty() || text.endsWith (label) ? text : text + " " + label).trim();
    }

    std::vector<Point> getPoints (te::AutomatableParameter& parameter)
    {
        auto& curve = parameter.getCurve();
        std::vector<Point> points;
        points.reserve ((size_t) curve.getNumPoints());

        for (int i = 0; i < curve.getNumPoints(); ++i)
            points.push_back ({ toBeat (curve, curve.getPointPosition (i)), to01 (parameter, curve.getPointValue (i)),
                                curve.getPointCurve (i) });

        return points;
    }

    bool hasPoints (const te::AutomatableParameter& parameter)
    {
        return parameter.getCurve().getNumPoints() > 0;
    }

    juce::Array<te::AutomatableParameter*> getAutomated (te::AudioTrack& track)
    {
        auto parameters = getParameters (track);
        parameters.removeIf ([] (te::AutomatableParameter* p) { return ! hasPoints (*p); });
        return parameters;
    }

    float getValueAt (te::AutomatableParameter& parameter, double beat)
    {
        auto& curve = parameter.getCurve();

        if (curve.getNumPoints() == 0)
            return to01 (parameter, parameter.getCurrentExplicitValue());

        return to01 (parameter, curve.getValueAt (toCurvePosition (curve, beat), parameter.getCurrentExplicitValue()));
    }

    int addPoint (te::AutomatableParameter& parameter, double beat, float value01)
    {
        auto& curve = parameter.getCurve();
        const auto position = toCurvePosition (curve, beat);

        // On a bent stretch both halves go on bending its way.
        auto bend = 0.0f;

        for (int i = 0; i + 1 < curve.getNumPoints(); ++i)
            if (te::toUnderlying (curve.getPointPosition (i)) < te::toUnderlying (position)
                && te::toUnderlying (position) < te::toUnderlying (curve.getPointPosition (i + 1)))
                bend = curve.getPointCurve (i);

        return curve.addPoint (position, toValue (parameter, value01), bend, getUndoManager (parameter));
    }

    void movePoint (te::AutomatableParameter& parameter, int index, double beat, float value01)
    {
        auto& curve = parameter.getCurve();

        if (! juce::isPositiveAndBelow (index, curve.getNumPoints()))
            return;

        curve.movePoint (index, toCurvePosition (curve, beat), toValue (parameter, value01),
                         parameter.getValueRange(), false, getUndoManager (parameter));
    }

    void removePoint (te::AutomatableParameter& parameter, int index)
    {
        auto& curve = parameter.getCurve();

        if (juce::isPositiveAndBelow (index, curve.getNumPoints()))
        {
            curve.removePoint (index, getUndoManager (parameter));
            followKnobIfNoLine (parameter);
        }
    }

    void movePoints (te::AutomatableParameter& parameter, const std::vector<int>& indices, const std::vector<Point>& from,
                     double beatShift, float valueShift)
    {
        auto& curve = parameter.getCurve();
        const auto count = (int) from.size();

        if (count != curve.getNumPoints())
            return;

        std::vector<bool> moves (from.size(), false);

        for (const auto index : indices)
            if (juce::isPositiveAndBelow (index, count))
                moves[(size_t) index] = true;

        // No point that moves passes one that stays (or beat 0), none leaves the range.
        auto first = -1;

        for (int i = 0; i < count; ++i)
        {
            if (! moves[(size_t) i])
                continue;

            auto earliest = 0.0, latest = std::numeric_limits<double>::max();

            for (int j = i; --j >= 0;)
                if (! moves[(size_t) j]) { earliest = from[(size_t) j].beat; break; }

            for (int j = i + 1; j < count; ++j)
                if (! moves[(size_t) j]) { latest = from[(size_t) j].beat; break; }

            const auto& point = from[(size_t) i];
            beatShift = juce::jlimit (std::min (0.0, earliest - point.beat), std::max (0.0, latest - point.beat), beatShift);
            valueShift = juce::jlimit (-point.value, 1.0f - point.value, valueShift);
            first = first < 0 ? i : first;
        }

        if (first < 0)
            return;

        // Each makes room for the next: going later the last moves first, going earlier the first.
        const auto move = [&] (int i)
        {
            if (moves[(size_t) i])
                movePoint (parameter, i, from[(size_t) i].beat + beatShift, from[(size_t) i].value + valueShift);
        };

        if (from[(size_t) first].beat + beatShift > toBeat (curve, curve.getPointPosition (first)))
            for (int i = count; --i >= 0;)
                move (i);
        else
            for (int i = 0; i < count; ++i)
                move (i);
    }

    void removePoints (te::AutomatableParameter& parameter, std::vector<int> indices)
    {
        std::sort (indices.begin(), indices.end());
        indices.erase (std::unique (indices.begin(), indices.end()), indices.end());

        for (auto index = indices.rbegin(); index != indices.rend(); ++index)
            removePoint (parameter, *index);
    }

    std::vector<Point> copyPoints (te::AutomatableParameter& parameter, std::vector<int> indices)
    {
        std::sort (indices.begin(), indices.end());
        indices.erase (std::unique (indices.begin(), indices.end()), indices.end());

        const auto points = getPoints (parameter);
        std::vector<Point> shape;

        for (const auto index : indices)
            if (juce::isPositiveAndBelow (index, (int) points.size()))
                shape.push_back (points[(size_t) index]);

        if (shape.empty())
            return shape;

        const auto start = shape.front().beat;

        for (auto& point : shape)
            point.beat -= start;

        // The last point's bend was towards a point that isn't part of the shape.
        shape.back().curve = 0.0f;
        return shape;
    }

    std::vector<int> pastePoints (te::AutomatableParameter& parameter, const std::vector<Point>& shape, double beat)
    {
        std::vector<int> pasted;

        if (shape.empty())
            return pasted;

        auto& curve = parameter.getCurve();
        auto* undoManager = getUndoManager (parameter);
        beat = std::max (0.0, beat);
        const auto endBeat = beat + shape.back().beat;
        constexpr double tolerance = 1.0e-6;

        for (int i = curve.getNumPoints(); --i >= 0;)
            if (const auto at = toBeat (curve, curve.getPointPosition (i)); at > beat - tolerance && at < endBeat + tolerance)
                curve.removePoint (i, undoManager);

        for (const auto& point : shape)
            pasted.push_back (curve.addPoint (toCurvePosition (curve, beat + point.beat),
                                              toValue (parameter, juce::jlimit (0.0f, 1.0f, point.value)), point.curve, undoManager));

        return pasted;
    }

    void setCurve (te::AutomatableParameter& parameter, int index, float bend)
    {
        auto& curve = parameter.getCurve();

        if (juce::isPositiveAndBelow (index, curve.getNumPoints() - 1))
            curve.setCurveValue (index, juce::jlimit (-1.0f, 1.0f, bend), getUndoManager (parameter));
    }

    std::optional<Point> getBendHandle (te::AutomatableParameter& parameter, int index)
    {
        auto& curve = parameter.getCurve();

        if (! juce::isPositiveAndBelow (index, curve.getNumPoints() - 1)
            || te::toUnderlying (curve.getPointPosition (index)) >= te::toUnderlying (curve.getPointPosition (index + 1))
            || curve.getPointValue (index) == curve.getPointValue (index + 1))
            return std::nullopt;

        const auto handle = curve.getBezierHandle (index);
        return Point { toBeat (curve, handle.time), to01 (parameter, handle.value), curve.getPointCurve (index) };
    }

    float getCurveForHandle (te::AutomatableParameter& parameter, int index, float value01)
    {
        auto& curve = parameter.getCurve();

        if (! juce::isPositiveAndBelow (index, curve.getNumPoints() - 1))
            return 0.0f;

        // The handle is the bend's share of the way from the middle of the straight line to the
        // corner the line bends towards: below the middle of a rise, or above the middle of a fall,
        // when the value changes late.
        const auto from = curve.getPointValue (index), to = curve.getPointValue (index + 1);
        const auto middle = 0.5f * (from + to), half = 0.5f * std::abs (to - from);

        if (half <= 0.0f)
            return 0.0f;

        const auto above = (toValue (parameter, value01) - middle) / half;
        return juce::jlimit (-1.0f, 1.0f, to > from ? -above : above);
    }

    void clear (te::AutomatableParameter& parameter)
    {
        parameter.getCurve().clear (getUndoManager (parameter));
        followKnobIfNoLine (parameter);
    }
}
