#include "ui/PluginWindow.h"
#include "audio/Effects.h"
#include "audio/PluginSafety.h"
#include "sandbox/RemotePlugin.h"
#include "ui/DrumPadsPanel.h"
#include "ui/ParameterPanel.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    PluginWindow::PluginWindow (te::Plugin& p)
        : juce::DocumentWindow (audio::effects::getDisplayName (p), theme::colour::background,
                                juce::DocumentWindow::closeButton, true),
          plugin (p),
          // A plugin in a process of its own has its editor there (RemotePluginWindow): here, knobs.
          editor (audio::plugin_safety::getRemote (p) == nullptr ? p.createEditor() : nullptr)
    {
        setUsingNativeTitleBar (true);

        if (editor != nullptr)
        {
            setContentNonOwned (editor.get(), true);
            setResizable (editor->allowWindowResizing(), false);
        }
        else
        {
            // The drum machine: its knobs, and its drums to put samples on.
            if (dynamic_cast<audio::drum_kit::Plugin*> (&plugin) != nullptr)
                knobs = std::make_unique<DrumPadsPanel> (plugin);
            else
                knobs = std::make_unique<ParameterPanel> (plugin);

            setContentNonOwned (knobs.get(), true);
            setResizable (true, false);
            setResizeLimits (360, 120, 1200, 1600);
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
        knobs.reset();
    }

    void PluginWindow::closeButtonPressed()
    {
        plugin.windowState->closeWindowExplicitly();
    }

    void PluginWindow::moved()
    {
        plugin.windowState->lastWindowBounds = getBounds();
    }

    //==============================================================================
    std::unique_ptr<RemotePluginWindow> RemotePluginWindow::open (te::Plugin& plugin)
    {
        auto* remote = audio::plugin_safety::getRemote (plugin);

        if (remote == nullptr || ! remote->hasEditor()
             || ! remote->showEditor (plugin.windowState->choosePositionForPluginWindow(),
                                      audio::effects::getDisplayName (plugin), true))
            return {};

        return std::unique_ptr<RemotePluginWindow> (new RemotePluginWindow (plugin));
    }

    RemotePluginWindow::RemotePluginWindow (te::Plugin& p) : plugin (p)
    {
        // Tracktion takes a window that isn't visible for one that was closed.
        setVisible (true);

        if (auto* remote = audio::plugin_safety::getRemote (plugin))
        {
            remote->onEditorClosed = [&state = *plugin.windowState] (juce::Rectangle<int> lastBounds)
            {
                if (! lastBounds.isEmpty())
                    state.lastWindowBounds = lastBounds;

                state.closeWindowExplicitly();
            };
        }
    }

    RemotePluginWindow::~RemotePluginWindow()
    {
        // The plugin outlives its window state, and so this; its stand-in may be a new one.
        if (auto* remote = audio::plugin_safety::getRemote (plugin))
        {
            remote->onEditorClosed = nullptr;

            if (const auto bounds = remote->hideEditor(); ! bounds.isEmpty())
                plugin.windowState->lastWindowBounds = bounds;
        }

        plugin.edit.flushPluginStateIfNeeded (plugin);
    }

    //==============================================================================
    std::unique_ptr<juce::Component> HarmoniaUIBehaviour::createPluginWindow (te::PluginWindowState& state)
    {
        auto* windowState = dynamic_cast<te::Plugin::WindowState*> (&state);

        if (windowState == nullptr)
            return {};

        auto& plugin = windowState->plugin;

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
            if (external->getAudioPluginInstance() == nullptr)
                return {};

        // A plugin in a process of its own shows its editor there; one that crashed shows nothing
        // until it is reloaded.
        if (auto* remote = audio::plugin_safety::getRemote (plugin))
        {
            if (remote->hasCrashed())
                return {};

            if (remote->hasEditor())
                if (auto window = RemotePluginWindow::open (plugin))
                    return window;
        }

        return std::make_unique<PluginWindow> (plugin);
    }
}
