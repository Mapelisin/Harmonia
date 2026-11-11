#include "sandbox/TestPluginSong.h"
#include "audio/Effects.h"
#include "ui/CpuMeter.h"
#include "ui/MainComponent.h"
#include "ui/PluginWindow.h"
#include "ui/PluginWindows.h"

using namespace harmonia;
namespace windows = ui::plugin_windows;
namespace effects = audio::effects;

namespace
{
    te::Plugin& addEffect (te::PluginList& chain, const juce::String& name)
    {
        for (const auto& type : effects::getBuiltIn())
        {
            if (type.name == name)
            {
                juce::String error;
                auto* plugin = effects::add (chain, type, error);
                REQUIRE_MESSAGE (plugin != nullptr, error.toStdString());
                return *plugin;
            }
        }

        FAIL ("no built-in effect called " << name.toStdString());
        std::abort();
    }

    /** Opens the plugin's window the way the program does (the tests' engine opens none itself). */
    void open (te::Plugin& plugin)
    {
        ui::HarmoniaUIBehaviour behaviour;
        plugin.windowState->pluginWindow = behaviour.createPluginWindow (*plugin.windowState);
        REQUIRE (plugin.windowState->pluginWindow != nullptr);
    }

    ui::PluginWindow& windowOf (te::Plugin& plugin)
    {
        auto* window = dynamic_cast<ui::PluginWindow*> (plugin.windowState->pluginWindow.get());
        REQUIRE (window != nullptr);
        return *window;
    }

    /** The tests share the program's settings: "on top" goes back to what it was. */
    struct OnTopSetting
    {
        ~OnTopSetting()    { windows::setStaysOnTop (engine, was); }

        te::Engine& engine = test::session().getEngine();
        const bool was = windows::staysOnTop (engine);
    };

    /** A song with the test synth (in a process of its own) on "Lead", an EQ after it and a
        limiter on the master. */
    struct WindowSong
    {
        WindowSong()    { song.track.setName ("Lead"); }
        ~WindowSong()   { windows::closeAll (edit); }

        test::plugins::SynthSong song;
        te::Edit& edit = song.project->getEdit();
        te::Plugin& synth = song.instrument();
        te::Plugin& eq = addEffect (song.track.pluginList, "EQ");
        te::Plugin& limiter = addEffect (edit.getMasterPluginList(), "Limiter");
    };

    std::optional<juce::PopupMenu::Item> find (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text.startsWith (text))
                return it.getItem();

        return std::nullopt;
    }
}

TEST_SUITE ("ui: the plugin windows together")
{
    TEST_CASE ("the open ones are listed in the song's order, named with their tracks, and closed all at once")
    {
        WindowSong s;
        CHECK (windows::getOpen (s.edit).isEmpty());
        CHECK (windows::closeAll (s.edit) == 0);

        open (s.limiter);
        open (s.synth);

        const auto opened = windows::getOpen (s.edit);
        REQUIRE (opened.size() == 2);
        CHECK (opened[0] == &s.synth);
        CHECK (opened[1] == &s.limiter);
        CHECK (windows::describe (s.synth) == "Lead: " + effects::getDisplayName (s.synth));
        CHECK (windows::describe (s.limiter) == "Master: Limiter");
        CHECK (windows::describe (s.eq) == "Lead: EQ");
        CHECK (s.song.remote().isEditorShowing());

        CHECK (windows::closeAll (s.edit) == 2);
        CHECK (windows::getOpen (s.edit).isEmpty());
        CHECK (s.limiter.windowState->pluginWindow == nullptr);
        CHECK_FALSE (s.song.remote().isEditorShowing());
    }

    TEST_CASE ("kept on top: the windows that are open and those opened later, Harmonia's and the plugin's own process's")
    {
        WindowSong s;
        const OnTopSetting restore;
        auto& engine = restore.engine;
        auto& remote = s.song.remote();

        windows::setStaysOnTop (engine, false);
        CHECK_FALSE (windows::staysOnTop (engine));

        open (s.eq);
        open (s.synth);
        CHECK_FALSE (windowOf (s.eq).isAlwaysOnTop());
        CHECK_FALSE (remote.isEditorAlwaysOnTop());

        // Set while they are open: they rise, and stay open where they were.
        windows::setStaysOnTop (engine, true);
        CHECK (windows::staysOnTop (engine));
        CHECK (windowOf (s.eq).isAlwaysOnTop());
        CHECK (remote.isEditorShowing());
        CHECK (remote.isEditorAlwaysOnTop());
        CHECK (windows::getOpen (s.edit).size() == 2);

        // Opened while it is set: on top from the start.
        CHECK (windows::closeAll (s.edit) == 2);
        CHECK_FALSE (remote.isEditorAlwaysOnTop());
        open (s.eq);
        open (s.synth);
        CHECK (windowOf (s.eq).isAlwaysOnTop());
        CHECK (remote.isEditorAlwaysOnTop());

        // Brought to the front, it stays so.
        windows::bringToFront (s.synth);
        CHECK (remote.isEditorAlwaysOnTop());

        windows::setStaysOnTop (engine, false);
        CHECK_FALSE (windowOf (s.eq).isAlwaysOnTop());
        CHECK_FALSE (remote.isEditorAlwaysOnTop());
        CHECK (remote.isEditorShowing());
    }

    TEST_CASE ("View > Plugin windows: the open ones, Close all (F12) and the tick that keeps them on top; the CPU meter is a button")
    {
        WindowSong s;
        const OnTopSetting restore;
        windows::setStaysOnTop (restore.engine, false);

        ui::MainComponent main (test::session(), *s.song.project);
        main.setBounds (0, 0, 1280, 800);
        auto& menu = main.getMainMenu();

        auto view = menu.createViewMenu();
        CHECK (find (view, "None are open").has_value());
        REQUIRE (find (view, "Close all plugin windows").has_value());
        CHECK_FALSE (find (view, "Close all plugin windows")->isEnabled);
        CHECK (find (view, "Close all plugin windows")->shortcutKeyDescription == "F12");
        CHECK (find (view, "Plugin load").has_value());
        CHECK (main.getCpuMeter().onClick != nullptr);

        open (s.limiter);
        open (s.eq);
        view = menu.createViewMenu();
        CHECK_FALSE (find (view, "None are open").has_value());
        CHECK (find (view, "Lead: EQ").has_value());
        CHECK (find (view, "Master: Limiter").has_value());
        CHECK (find (view, "Close all plugin windows")->isEnabled);

        // F12 closes them and says how many.
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::F12Key)));
        CHECK (windows::getOpen (s.edit).isEmpty());
        CHECK (main.getStatusBar().getMessage() == "Closed 2 plugin windows");
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::F12Key)));
        CHECK (main.getStatusBar().getMessage() == "No plugin windows are open");

        // The menu's item does the same.
        open (s.eq);
        const auto close = find (menu.createViewMenu(), "Close all plugin windows");
        REQUIRE (close->action != nullptr);
        close->action();
        CHECK (windows::getOpen (s.edit).isEmpty());
        CHECK (main.getStatusBar().getMessage() == "Closed 1 plugin window");

        // The tick.
        auto keep = find (menu.createViewMenu(), "Keep plugin windows on top");
        REQUIRE (keep.has_value());
        CHECK_FALSE (keep->isTicked);
        keep->action();
        CHECK (windows::staysOnTop (restore.engine));
        CHECK (main.getStatusBar().getMessage() == "Plugin windows now stay above every other window");

        keep = find (menu.createViewMenu(), "Keep plugin windows on top");
        CHECK (keep->isTicked);
        open (s.eq);
        CHECK (windowOf (s.eq).isAlwaysOnTop());
        keep->action();
        CHECK_FALSE (windows::staysOnTop (restore.engine));
        CHECK_FALSE (windowOf (s.eq).isAlwaysOnTop());
    }
}
