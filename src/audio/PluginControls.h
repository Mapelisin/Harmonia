#pragma once

#include "core/Tracktion.h"

namespace harmonia::audio
{
    /** One setting of a plugin, for Harmonia's own knobs window (plugins without an editor of their
        own: the built-in synth and effects, some VSTs).

        Usually an automatable parameter; a few built-in effects keep settings as plain properties
        instead (chorus, phaser, delay time, filter type), those are covered too.
        Changes go through the edit's undo manager: wrap a drag in beginChange / endChange to make
        it one undo step. */
    class PluginControl
    {
    public:
        /** Every control of a plugin, in the plugin's order. */
        static std::vector<PluginControl> getAll (te::Plugin&);

        juce::String getName() const;

        float getValue() const;
        void setValue (float);

        juce::NormalisableRange<float> getRange() const     { return range; }
        float getDefaultValue() const                       { return defaultValue; }
        juce::String getText (float value) const;

        /** Non-empty for a control that picks one of a few options; the value is the option's index. */
        const juce::StringArray& getChoices() const         { return choices; }

        void beginChange();
        void endChange();

        te::Plugin& getPlugin() const                       { return *plugin; }

        /** The automatable parameter behind it (nullptr for a property control). */
        te::AutomatableParameter* getParameter() const      { return parameter.get(); }

    private:
        PluginControl() = default;

        te::Plugin::Ptr plugin;
        te::AutomatableParameter::Ptr parameter;

        // A property control (no parameter):
        juce::Identifier property;
        juce::String name, unit;
        juce::StringArray choiceValues;     ///< what a choice stores in the property
        bool isInteger = false;

        juce::NormalisableRange<float> range { 0.0f, 1.0f };
        float defaultValue = 0.0f;
        juce::StringArray choices;

        static PluginControl forParameter (te::Plugin&, te::AutomatableParameter&);
        static PluginControl forProperty (te::Plugin&, const juce::Identifier&, const juce::String& name,
                                          juce::NormalisableRange<float>, float defaultValue, const juce::String& unit,
                                          bool isInteger = false);
        static PluginControl forChoice (te::Plugin&, const juce::Identifier&, const juce::String& name,
                                        const juce::StringArray& labels, const juce::StringArray& values);
    };
}
