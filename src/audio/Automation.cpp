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
            points.push_back ({ toBeat (curve, curve.getPointPosition (i)), to01 (parameter, curve.getPointValue (i)) });

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
        return curve.addPoint (toCurvePosition (curve, beat), toValue (parameter, value01), 0.0f, getUndoManager (parameter));
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

    void clear (te::AutomatableParameter& parameter)
    {
        parameter.getCurve().clear (getUndoManager (parameter));
        followKnobIfNoLine (parameter);
    }
}
