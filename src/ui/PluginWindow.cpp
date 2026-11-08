#include "ui/PluginWindow.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    PluginWindow::PluginWindow (te::Plugin& p)
        : juce::DocumentWindow (p.getName(), theme::colour::background, juce::DocumentWindow::closeButton, true),
          plugin (p),
          editor (p.createEditor())
    {
        setUsingNativeTitleBar (true);

        if (editor != nullptr)
        {
            setContentNonOwned (editor.get(), true);
            setResizable (editor->allowWindowResizing(), false);
        }

        setTopLeftPosition (plugin.windowState->choosePositionForPluginWindow());
        setVisible (true);
        toFront (true);
    }

    PluginWindow::~PluginWindow()
    {
        plugin.edit.flushPluginStateIfNeeded (plugin);
        clearContentComponent();
        editor.reset();
    }

    void PluginWindow::closeButtonPressed()
    {
        plugin.windowState->closeWindowExplicitly();
    }

    void PluginWindow::moved()
    {
        plugin.windowState->lastWindowBounds = getBounds();
    }

    std::unique_ptr<juce::Component> HarmoniaUIBehaviour::createPluginWindow (te::PluginWindowState& state)
    {
        auto* windowState = dynamic_cast<te::Plugin::WindowState*> (&state);

        if (windowState == nullptr)
            return {};

        auto& plugin = windowState->plugin;

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
            if (external->getAudioPluginInstance() == nullptr)
                return {};

        auto window = std::make_unique<PluginWindow> (plugin);

        if (! window->hasEditor())
            return {};

        return window;
    }
}
