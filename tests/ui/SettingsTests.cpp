#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "audio/PluginSafety.h"
#include "core/Project.h"
#include "sandbox/TestPluginSong.h"
#include "ui/ExportOptions.h"
#include "ui/settings/SettingsPanel.h"

using namespace harmonia;
using ui::MidiSettingsTab;
using ui::SettingsPanel;
using Keyboard = audio::MidiInputs::Keyboard;

namespace
{
    /** A click, and the message loop that delivers it. */
    void click (juce::Button& button)
    {
        button.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    /** The test synth on a new song's track: where it runs. */
    audio::plugin_safety::Status loadTestSynth()
    {
        const test::plugins::KnownTestPlugins known;
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        const auto error = audio::instruments::useExternal (track, test::plugins::synth());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        auto* plugin = audio::instruments::get (track);
        REQUIRE (plugin != nullptr);
        return audio::plugin_safety::getStatus (*plugin);
    }
}

TEST_SUITE ("ui::Settings")
{
    TEST_CASE ("the audio tab tells the latency of the buffer chosen")
    {
        const auto small = ui::AudioSettingsTab::describeLatency (512, 0, 48000.0);
        CHECK (small.contains ("10.7 ms"));
        CHECK (small.contains ("512 samples"));
        CHECK (small.contains ("48000 Hz"));
        CHECK (small.contains ("instant"));

        // What the driver reports counts when it is more than the buffer.
        const auto large = ui::AudioSettingsTab::describeLatency (2048, 2205, 44100.0);
        CHECK (large.contains ("50.0 ms"));
        CHECK (large.contains ("late"));

        CHECK (ui::AudioSettingsTab::describeLatency (0, 0, 0.0).startsWith ("No sound device"));

        // The tests open no sound card: the tab says so instead of showing an empty selector.
        ui::AudioSettingsTab tab (test::session());
        CHECK (tab.getLatencyText().contains ("without sound devices"));
    }

    TEST_CASE ("the MIDI tab lists the keyboards with a switch each, and a switch reaches the engine")
    {
        std::vector<Keyboard> keyboards { { "in-1", "Keystation 49", true }, { "in-2", "Drum Pads", false } };
        std::vector<std::pair<juce::String, bool>> switched;

        MidiSettingsTab tab ({ [&] { return keyboards; },
                               [&] (const juce::String& id, bool on) { switched.emplace_back (id, on); } });
        tab.setSize (500, 400);

        REQUIRE (tab.getNumKeyboards() == 2);
        CHECK (tab.getSwitch (0)->getButtonText() == "Keystation 49");
        CHECK (tab.getSwitch (0)->getToggleState());
        CHECK (! tab.getSwitch (1)->getToggleState());
        CHECK (tab.getMessage().contains ("track being edited"));

        click (*tab.getSwitch (1));
        REQUIRE (switched.size() == 1);
        CHECK (switched[0] == std::pair<juce::String, bool> ("in-2", true));

        click (*tab.getSwitch (0));
        REQUIRE (switched.size() == 2);
        CHECK (switched[1] == std::pair<juce::String, bool> ("in-1", false));

        // Unplugged: the list follows.
        keyboards.clear();
        tab.refresh();
        CHECK (tab.getNumKeyboards() == 0);
        CHECK (tab.getMessage().startsWith ("No MIDI keyboard"));
    }

    TEST_CASE ("the plugins tab lists the folders; one of one's own is removed, a standard one can't be")
    {
        auto& session = test::session();
        const juce::TemporaryFile folder;
        REQUIRE (folder.getFile().createDirectory());
        audio::PluginScanner::addFolder (session, folder.getFile());

        ui::PluginSettingsTab tab (session);
        tab.setSize (560, 440);

        const auto rows = tab.getFolderRows();
        const auto own = rows.indexOf (folder.getFile().getFullPathName());
        REQUIRE (own >= 0);

        // The standard folders come first, marked.
        for (int i = 0; i < own; ++i)
            CHECK (rows[i].endsWith ("(standard)"));

        tab.getFolderList().selectRow (0);
        CHECK (tab.getRemoveButton().isEnabled() == ! rows[0].endsWith ("(standard)"));

        tab.getFolderList().selectRow (own);
        CHECK (tab.getRemoveButton().isEnabled());
        click (tab.getRemoveButton());

        CHECK (! tab.getFolderRows().contains (folder.getFile().getFullPathName()));
        CHECK (! audio::PluginScanner::getOwnFolders (session).contains (folder.getFile().getFullPathName()));
        CHECK (tab.getMessage().contains ("won't look in"));
    }

    TEST_CASE ("with its switch off, plugins load into Harmonia; on, into processes of their own")
    {
        auto& session = test::session();
        REQUIRE (session.isPluginSandboxOn());

        ui::PluginSettingsTab tab (session);
        REQUIRE (tab.getSandboxSwitch().isEnabled());
        CHECK (tab.getSandboxSwitch().getToggleState());

        click (tab.getSandboxSwitch());
        CHECK (! session.isPluginSandboxOn());
        CHECK (tab.getMessage().contains ("crash"));
        CHECK (loadTestSynth() == audio::plugin_safety::Status::notSandboxed);

        // Kept for the next start.
        CHECK (! session.getEngine().getPropertyStorage().getPropertiesFile().getBoolValue ("pluginSandbox", true));

        click (tab.getSandboxSwitch());
        CHECK (session.isPluginSandboxOn());
        CHECK (loadTestSynth() == audio::plugin_safety::Status::running);
        CHECK (session.getEngine().getPropertyStorage().getPropertiesFile().getBoolValue ("pluginSandbox", false));
    }

    TEST_CASE ("the settings window opens at the tab asked for and switches tabs")
    {
        SettingsPanel panel (test::session(), SettingsPanel::Tab::plugins);
        CHECK (panel.getCurrentTab() == SettingsPanel::Tab::plugins);
        CHECK (panel.getPluginTab().isShowing() == panel.isShowing());

        panel.showTab (SettingsPanel::Tab::midi);
        CHECK (panel.getCurrentTab() == SettingsPanel::Tab::midi);

        panel.showTab (SettingsPanel::Tab::audio);
        CHECK (panel.getCurrentTab() == SettingsPanel::Tab::audio);
        CHECK (panel.getWidth() > 0);
    }

    TEST_CASE ("the settings window paints each tab")
    {
        const juce::TemporaryFile folder;
        REQUIRE (folder.getFile().createDirectory());
        audio::PluginScanner::addFolder (test::session(), folder.getFile());

        SettingsPanel panel (test::session(), SettingsPanel::Tab::plugins);
        const std::pair<SettingsPanel::Tab, const char*> tabs[] { { SettingsPanel::Tab::audio, "settings-audio.png" },
                                                                  { SettingsPanel::Tab::midi, "settings-midi.png" },
                                                                  { SettingsPanel::Tab::plugins, "settings-plugins.png" } };

        for (const auto& [tab, name] : tabs)
        {
            panel.showTab (tab);
            const auto image = panel.createComponentSnapshot (panel.getLocalBounds());
            CHECK (image.isValid());

            // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
            if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
            {
                juce::FileOutputStream out (juce::File (dir).getChildFile (name));
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }

        audio::PluginScanner::removeFolder (test::session(), folder.getFile().getFullPathName());
    }

    TEST_CASE ("the export window paints")
    {
        ui::ExportOptions options ({ audio::ExportFormat::Type::mp3, 16, 320 });
        const auto image = options.createComponentSnapshot (options.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("export-options.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
