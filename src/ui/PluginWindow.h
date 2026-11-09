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

    /** Stands for the window of a plugin that runs in a process of its own: that process shows
        the plugin's editor, this is what Tracktion keeps as its window (never on screen). Closing
        one closes the other. */
    class RemotePluginWindow final : public juce::Component
    {
    public:
        /** Shows the editor: nullptr (and nothing shown) if it couldn't. */
        static std::unique_ptr<RemotePluginWindow> open (te::Plugin&);
        ~RemotePluginWindow() override;

    private:
        explicit RemotePluginWindow (te::Plugin&);
        te::Plugin& plugin;

        JUCE_DECLARE_NON_COPYABLE (RemotePluginWindow)
    };

    /** Tells Tracktion how Harmonia opens plugin windows. */
    class HarmoniaUIBehaviour final : public te::UIBehaviour
    {
    public:
        std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState&) override;
    };
}
