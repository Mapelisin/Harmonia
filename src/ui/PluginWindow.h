#pragma once

#include "core/Tracktion.h"

namespace harmonia::ui
{
    /** A top-level window hosting a plugin's own editor or, for plugins without one (the built-in
        synth and effects), Harmonia's knobs (ParameterPanel). */
    class PluginWindow final : public juce::DocumentWindow
    {
    public:
        explicit PluginWindow (te::Plugin&);
        ~PluginWindow() override;

        bool hasEditor() const noexcept     { return editor != nullptr; }
        bool showsKnobs() const noexcept    { return knobs != nullptr; }

    private:
        te::Plugin& plugin;
        std::unique_ptr<te::Plugin::EditorComponent> editor;
        std::unique_ptr<juce::Component> knobs;

        void closeButtonPressed() override;
        void moved() override;

        JUCE_DECLARE_NON_COPYABLE (PluginWindow)
    };

    /** Tells Tracktion how Harmonia opens plugin windows. */
    class HarmoniaUIBehaviour final : public te::UIBehaviour
    {
    public:
        std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState&) override;
    };
}
