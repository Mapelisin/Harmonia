#include "audio/PluginControls.h"
#include "audio/AutomationWriter.h"

namespace harmonia::audio
{
    namespace
    {
        juce::NormalisableRange<float> skewed (float start, float end, float centre)
        {
            juce::NormalisableRange<float> range (start, end);
            range.setSkewForCentre (centre);
            return range;
        }
    }

    std::vector<PluginControl> PluginControl::getAll (te::Plugin& plugin)
    {
        std::vector<PluginControl> result;

        // Settings Tracktion keeps as plain properties, first (they are the main ones).
        if (dynamic_cast<te::LowPassPlugin*> (&plugin) != nullptr)
        {
            result.push_back (forChoice (plugin, te::IDs::mode, "Type", { "Low-pass", "High-pass" }, { "lowpass", "highpass" }));
        }
        else if (dynamic_cast<te::DelayPlugin*> (&plugin) != nullptr)
        {
            result.push_back (forProperty (plugin, te::IDs::length, "Time", skewed (10.0f, 2000.0f, 300.0f), 150.0f, "ms", true));
        }
        else if (dynamic_cast<te::ChorusPlugin*> (&plugin) != nullptr)
        {
            result.push_back (forProperty (plugin, te::IDs::depthMs, "Depth", { 0.1f, 10.0f }, 3.0f, "ms"));
            result.push_back (forProperty (plugin, te::IDs::speedHz, "Speed", skewed (0.1f, 10.0f, 1.0f), 1.0f, "Hz"));
            result.push_back (forProperty (plugin, te::IDs::width, "Width", { 0.0f, 1.0f }, 0.5f, "%"));
            result.push_back (forProperty (plugin, te::IDs::mixProportion, "Mix", { 0.0f, 1.0f }, 0.5f, "%"));
        }
        else if (dynamic_cast<te::PhaserPlugin*> (&plugin) != nullptr)
        {
            result.push_back (forProperty (plugin, te::IDs::depth, "Depth", { 0.5f, 8.0f }, 5.0f, "oct"));
            result.push_back (forProperty (plugin, te::IDs::rate, "Rate", skewed (0.05f, 5.0f, 0.5f), 0.4f, "Hz"));
            result.push_back (forProperty (plugin, te::IDs::feedback, "Feedback", { 0.0f, 0.95f }, 0.7f, "%"));
        }

        for (auto* parameter : plugin.getAutomatableParameters())
            result.push_back (forParameter (plugin, *parameter));

        return result;
    }

    PluginControl PluginControl::forParameter (te::Plugin& p, te::AutomatableParameter& parameter)
    {
        PluginControl c;
        c.plugin = &p;
        c.parameter = &parameter;
        c.range = parameter.valueRange;
        c.defaultValue = parameter.getDefaultValue().value_or (c.range.start);
        return c;
    }

    PluginControl PluginControl::forProperty (te::Plugin& p, const juce::Identifier& id, const juce::String& controlName,
                                              juce::NormalisableRange<float> valueRange, float defaultValueToUse,
                                              const juce::String& unitName, bool integer)
    {
        PluginControl c;
        c.plugin = &p;
        c.property = id;
        c.name = controlName;
        c.range = valueRange;
        c.defaultValue = defaultValueToUse;
        c.unit = unitName;
        c.isInteger = integer;
        return c;
    }

    PluginControl PluginControl::forChoice (te::Plugin& p, const juce::Identifier& id, const juce::String& controlName,
                                            const juce::StringArray& labels, const juce::StringArray& values)
    {
        PluginControl c;
        c.plugin = &p;
        c.property = id;
        c.name = controlName;
        c.choices = labels;
        c.choiceValues = values;
        c.range = { 0.0f, (float) (labels.size() - 1), 1.0f };
        return c;
    }

    juce::String PluginControl::getName() const
    {
        return parameter != nullptr ? parameter->getParameterName() : name;
    }

    float PluginControl::getValue() const
    {
        if (parameter != nullptr)
            return parameter->getCurrentValue();

        const auto value = plugin->state.getProperty (property);

        if (! choices.isEmpty())
            return (float) std::max (0, choiceValues.indexOf (value.toString()));

        return value.isVoid() ? defaultValue : (float) value;
    }

    void PluginControl::setValue (float value)
    {
        value = range.snapToLegalValue (value);

        if (parameter != nullptr)
        {
            parameter->setParameter (value, juce::sendNotification);
            return;
        }

        auto* undoManager = &plugin->edit.getUndoManager();

        if (! choices.isEmpty())
            plugin->state.setProperty (property, choiceValues[juce::roundToInt (value)], undoManager);
        else if (isInteger)
            plugin->state.setProperty (property, juce::roundToInt (value), undoManager);
        else
            plugin->state.setProperty (property, value, undoManager);
    }

    juce::String PluginControl::getText (float value) const
    {
        if (parameter != nullptr)
        {
            auto text = parameter->valueToString (value);
            const auto label = parameter->getLabel();
            return label.isEmpty() || text.endsWith (label) ? text : text + " " + label;
        }

        if (! choices.isEmpty())
            return choices[juce::roundToInt (value)];

        if (unit == "%")
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";

        if (isInteger)
            return juce::String (juce::roundToInt (value)) + " " + unit;

        return juce::String (value, std::abs (value) < 10.0f ? 2 : 1) + (unit.isEmpty() ? juce::String() : " " + unit);
    }

    void PluginControl::beginChange()
    {
        plugin->edit.getUndoManager().beginNewTransaction ("Change " + getName());

        if (parameter != nullptr)
            automation::beginTouch (*parameter);
    }

    void PluginControl::endChange()
    {
        if (parameter != nullptr)
            automation::endTouch (*parameter);
    }
}
