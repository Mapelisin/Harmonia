#pragma once

#include "core/Tracktion.h"

namespace harmonia::ui
{
    /** A top-level window hosting a plugin's own editor. */
    class PluginWindow final : public juce::DocumentWindow
    {
    public:
        explicit PluginWindow (te::Plugin&);
        ~PluginWindow() override;

        bool hasEditor() const noexcept     { return editor != nullptr; }

    private:
        te::Plugin& plugin;
        std::unique_ptr<te::Plugin::EditorComponent> editor;

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
