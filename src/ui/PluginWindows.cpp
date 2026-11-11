#include "ui/PluginWindows.h"
#include "audio/Effects.h"
#include "audio/PluginSafety.h"
#include "sandbox/RemotePlugin.h"
#include "ui/PluginWindow.h"

namespace harmonia::ui::plugin_windows
{
    namespace
    {
        const char* const onTopKey = "pluginWindowsOnTop";

        bool isOpen (te::Plugin& plugin)
        {
            return plugin.windowState != nullptr && plugin.windowState->isWindowShowing();
        }

        /** An open window, wherever it lives, goes above everything or back among the others. */
        void apply (te::Plugin& plugin, bool onTop)
        {
            if (auto* window = dynamic_cast<PluginWindow*> (plugin.windowState->pluginWindow.get()))
                window->setAlwaysOnTop (onTop);
            else if (auto* remote = audio::plugin_safety::getRemote (plugin))
                remote->setEditorAlwaysOnTop (onTop);
        }
    }

    juce::Array<te::Plugin*> getOpen (te::Edit& edit)
    {
        juce::Array<te::Plugin*> open;

        for (auto* plugin : audio::effects::getAllOfSong (edit))
            if (isOpen (*plugin))
                open.add (plugin);

        return open;
    }

    juce::String describe (te::Plugin& plugin)
    {
        const auto owner = audio::effects::getOwnerName (plugin);
        const auto name = audio::effects::getDisplayName (plugin);
        return owner.isNotEmpty() ? owner + ": " + name : name;
    }

    void bringToFront (te::Plugin& plugin)
    {
        audio::effects::showEditor (plugin);
    }

    int closeAll (te::Edit& edit)
    {
        const auto open = getOpen (edit);

        for (auto* plugin : open)
            plugin->windowState->closeWindowExplicitly();

        return open.size();
    }

    bool staysOnTop (te::Engine& engine)
    {
        return engine.getPropertyStorage().getPropertiesFile().getBoolValue (onTopKey, false);
    }

    void setStaysOnTop (te::Engine& engine, bool onTop)
    {
        auto& settings = engine.getPropertyStorage().getPropertiesFile();
        settings.setValue (onTopKey, onTop);
        settings.saveIfNeeded();

        for (auto* edit : engine.getActiveEdits().getEdits())
            if (edit != nullptr)
                for (auto* plugin : getOpen (*edit))
                    apply (*plugin, onTop);
    }
}
