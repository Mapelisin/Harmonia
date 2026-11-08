#pragma once

#include "Common.h"

namespace harmonia::spike
{
    /** A top-level window that hosts a plugin's own editor. */
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        explicit PluginWindow (te::Plugin&);
        ~PluginWindow() override;

        bool hasEditor() const noexcept { return editor != nullptr; }

    private:
        te::Plugin& plugin;
        std::unique_ptr<te::Plugin::EditorComponent> editor;

        void closeButtonPressed() override;
        void moved() override;
    };

    /** Tells Tracktion Engine how to open plugin windows. */
    class SpikeUIBehaviour : public te::UIBehaviour
    {
    public:
        std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState&) override;
    };
}
