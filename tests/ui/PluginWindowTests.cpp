#include "sandbox/TestPluginSong.h"
#include "audio/Export.h"
#include "ui/PluginWindow.h"

using namespace harmonia;

TEST_SUITE ("ui::PluginWindow")
{
    TEST_CASE ("a plugin in a process of its own shows its editor there; Tracktion keeps a stand-in that isn't on screen")
    {
        test::plugins::SynthSong song;
        auto& plugin = song.instrument();
        auto& remote = song.remote();
        ui::HarmoniaUIBehaviour behaviour;

        auto window = behaviour.createPluginWindow (*plugin.windowState);
        REQUIRE (dynamic_cast<ui::RemotePluginWindow*> (window.get()) != nullptr);
        CHECK (window->isVisible());
        CHECK (! window->isOnDesktop());
        CHECK (remote.isEditorShowing());

        // Closing Tracktion's window closes the editor, and where it was is kept for next time.
        window.reset();
        CHECK (! remote.isEditorShowing());
        REQUIRE (plugin.windowState->lastWindowBounds.has_value());
        CHECK (! plugin.windowState->lastWindowBounds->isEmpty());

        // Closed with its own close button (its process says so): Tracktion's window goes too.
        plugin.windowState->pluginWindow = behaviour.createPluginWindow (*plugin.windowState);
        CHECK (dynamic_cast<ui::RemotePluginWindow*> (plugin.windowState->pluginWindow.get()) != nullptr);
        CHECK (remote.isEditorShowing());
        REQUIRE (remote.onEditorClosed != nullptr);
        remote.onEditorClosed ({ 50, 60, 300, 200 });
        CHECK (plugin.windowState->pluginWindow == nullptr);
        CHECK (! remote.isEditorShowing());
        CHECK (remote.onEditorClosed == nullptr);
    }

    TEST_CASE ("a crashed plugin opens no window until it is reloaded")
    {
        test::plugins::SynthSong song;
        song.parameter (test::plugins::crash).setValueNotifyingHost (1.0f);

        const juce::TemporaryFile file (".wav");
        CHECK (audio::exportWav (song.project->getEdit(), file.getFile()).isEmpty());
        REQUIRE (song.remote().hasCrashed());

        ui::HarmoniaUIBehaviour behaviour;
        CHECK (behaviour.createPluginWindow (*song.instrument().windowState) == nullptr);

        song.parameter (test::plugins::crash).setValueNotifyingHost (0.0f);
        REQUIRE (audio::plugin_safety::reload (song.instrument()).isEmpty());
        CHECK (behaviour.createPluginWindow (*song.instrument().windowState) != nullptr);
    }
}
