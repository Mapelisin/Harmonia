#include "sandbox/TestPluginSong.h"

#include "audio/Effects.h"
#include "audio/PluginControls.h"
#include "audio/Presets.h"

using namespace harmonia;
namespace presets = audio::presets;
namespace effects = audio::effects;

namespace
{
    /** A presets folder of the test's own, deleted after it. */
    struct PresetsFolder
    {
        PresetsFolder()     { folder.createDirectory(); }
        ~PresetsFolder()    { folder.deleteRecursively(); }

        const juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getNonexistentChildFile ("harmonia-presets", {});
    };

    std::vector<float> valuesOf (te::Plugin& plugin)
    {
        std::vector<float> values;

        for (auto& control : audio::PluginControl::getAll (plugin))
            values.push_back (control.getValue());

        return values;
    }

    bool same (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i] - b[i]) > 1.0e-4f * std::max (1.0f, std::abs (a[i])))
                return false;

        return true;
    }

    /** Sets every control away from where it is: a choice to its next option, a number to
        `fraction` of its range. One undo step. */
    void turnEverything (te::Plugin& plugin, float fraction)
    {
        plugin.edit.getUndoManager().beginNewTransaction ("Turn");

        for (auto& control : audio::PluginControl::getAll (plugin))
        {
            if (const auto options = control.getChoices().size(); options > 0)
                control.setValue ((float) ((juce::roundToInt (control.getValue()) + 1) % options));
            else
                control.setValue (control.getRange().convertFrom0to1 (fraction));
        }
    }

    /** Lets messages through until `condition` holds (or the time is up): what it says then. */
    template <typename Condition>
    bool waitFor (Condition&& condition, int timeoutMs = 5000)
    {
        for (const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
             ! condition() && juce::Time::getMillisecondCounter() < until;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        return condition();
    }

    /** A song with the built-in synth on its first track, the drums on a second, every built-in
        effect on the first; no undo history. */
    struct BuiltInSong
    {
        BuiltInSong()
            : project (core::Project::createEmpty (test::session())),
              synthTrack (*project->getTracks().getFirst()),
              drumTrack (*project->addTrack ("Drums"))
        {
            REQUIRE (audio::instruments::useBuiltInSynth (synthTrack).isEmpty());
            REQUIRE (audio::instruments::useDrumKit (drumTrack).isEmpty());
            plugins.add (audio::instruments::get (synthTrack));
            plugins.add (audio::instruments::get (drumTrack));

            for (const auto& type : effects::getBuiltIn())
            {
                juce::String error;
                auto* effect = effects::add (synthTrack.pluginList, type, error);
                REQUIRE_MESSAGE (effect != nullptr, error.toStdString());
                plugins.add (effect);
            }

            project->clearUndoHistory();
        }

        te::Plugin* find (const juce::String& name)
        {
            for (auto* plugin : plugins)
                if (presets::getPluginName (*plugin) == name)
                    return plugin;

            FAIL_CHECK ("no plugin called " << name.toStdString());
            return nullptr;
        }

        std::unique_ptr<core::Project> project;
        te::AudioTrack& synthTrack;
        te::AudioTrack& drumTrack;
        juce::Array<te::Plugin*> plugins;
    };
}

TEST_SUITE ("presets")
{
    TEST_CASE ("every built-in instrument and effect keeps its settings as a preset, loaded as one undo step")
    {
        BuiltInSong song;
        PresetsFolder presetsFolder;
        REQUIRE (song.plugins.size() == effects::getBuiltIn().size() + 2);

        for (auto* plugin : song.plugins)
        {
            const auto name = presets::getPluginName (*plugin);
            CAPTURE (name.toStdString());
            REQUIRE (presets::canHavePresets (*plugin));
            CHECK (presets::canReset (*plugin));

            turnEverything (*plugin, 0.3f);
            const auto saved = valuesOf (*plugin);
            REQUIRE (presets::save (*plugin, "Mine", presetsFolder.folder).isEmpty());

            turnEverything (*plugin, 0.8f);
            const auto changed = valuesOf (*plugin);
            CHECK_FALSE (same (saved, changed));

            const auto files = presets::list (*plugin, presetsFolder.folder);
            REQUIRE (files.size() == 1);
            CHECK (presets::getName (files[0]) == "Mine");
            CHECK (files[0].getParentDirectory() == presets::getFolderFor (*plugin, presetsFolder.folder));

            REQUIRE (presets::load (*plugin, files[0]).isEmpty());
            CHECK (same (valuesOf (*plugin), saved));
            CHECK (song.project->getUndoDescription() == "Preset Mine");
            CHECK (presets::getLastUsed (*plugin) == "Mine");

            song.project->undo();
            CHECK (same (valuesOf (*plugin), changed));
            CHECK (presets::getLastUsed (*plugin) == "Mine");    // it was saved under that name

            song.project->redo();
            CHECK (same (valuesOf (*plugin), saved));
        }
    }

    TEST_CASE ("a preset is its plugin's: the list shows only its own, another plugin won't take it")
    {
        BuiltInSong song;
        PresetsFolder presetsFolder;
        auto* compressor = song.find ("Compressor");
        auto* reverb = song.find ("Reverb");
        REQUIRE ((compressor != nullptr && reverb != nullptr));

        REQUIRE (presets::save (*compressor, "Vocal", presetsFolder.folder).isEmpty());
        CHECK (presets::list (*compressor, presetsFolder.folder).size() == 1);
        CHECK (presets::list (*reverb, presetsFolder.folder).isEmpty());

        const auto file = presets::getFileFor (*compressor, "Vocal", presetsFolder.folder);
        const auto before = valuesOf (*reverb);
        CHECK (presets::load (*reverb, file).contains ("is a preset of Compressor"));
        CHECK (same (valuesOf (*reverb), before));
        CHECK_FALSE (song.project->canUndo());

        // A file that isn't a preset is said to be so, and left out of the list.
        const auto junk = presets::getFileFor (*compressor, "Junk", presetsFolder.folder);
        REQUIRE (junk.replaceWithText ("not a preset"));
        CHECK (presets::load (*compressor, junk).startsWith ("Not a Harmonia preset"));
        CHECK (presets::list (*compressor, presetsFolder.folder).size() == 1);

        // Nor does a plugin of the same name take another's file of the same preset name.
        juce::XmlElement other ("HARMONIAPRESET");
        other.setAttribute ("plugin", "VST3:Compressor:1234");
        REQUIRE (other.writeTo (presets::getFileFor (*compressor, "Theirs", presetsFolder.folder)));
        CHECK (presets::save (*compressor, "Theirs", presetsFolder.folder).contains ("another plugin's"));
        CHECK (presets::list (*compressor, presetsFolder.folder).size() == 1);
    }

    TEST_CASE ("names: characters Windows doesn't allow go, the same name replaces, the list is in name order")
    {
        BuiltInSong song;
        PresetsFolder presetsFolder;
        auto* eq = song.find ("EQ");
        REQUIRE (eq != nullptr);

        CHECK (presets::save (*eq, "   ", presetsFolder.folder) == "Type a name for the preset");
        REQUIRE (presets::save (*eq, "Bass: warm?", presetsFolder.folder).isEmpty());
        REQUIRE (presets::save (*eq, "Mix 10", presetsFolder.folder).isEmpty());
        REQUIRE (presets::save (*eq, "Mix 9", presetsFolder.folder).isEmpty());
        REQUIRE (presets::save (*eq, "Mix 9", presetsFolder.folder).isEmpty());

        juce::StringArray names;

        for (const auto& file : presets::list (*eq, presetsFolder.folder))
            names.add (presets::getName (file));

        CHECK (names.joinIntoString ("|") == "Bass warm|Mix 9|Mix 10");
        CHECK (presets::getFolderFor (*eq, presetsFolder.folder).getFileName() == "EQ");

        // The filter keeps one folder, low-pass or high-pass.
        CHECK (presets::getPluginName (*song.find ("Filter")) == "Filter");
    }

    TEST_CASE ("reset puts a built-in plugin's first settings back, as one undo step")
    {
        BuiltInSong song;
        auto* synth = audio::instruments::get (song.synthTrack);
        REQUIRE (synth != nullptr);

        const auto first = valuesOf (*synth);
        turnEverything (*synth, 0.7f);
        const auto changed = valuesOf (*synth);
        REQUIRE_FALSE (same (first, changed));

        presets::reset (*synth);
        CHECK (same (valuesOf (*synth), first));
        CHECK (song.project->getUndoDescription() == "Reset 4OSC");

        song.project->undo();
        CHECK (same (valuesOf (*synth), changed));
    }

    TEST_CASE ("a plugin in a process of its own: its settings go into a preset and come back, undo too")
    {
        test::plugins::SynthSong song;
        PresetsFolder presetsFolder;
        auto& plugin = song.instrument();
        auto& level = song.parameter (test::plugins::level);
        song.project->clearUndoHistory();

        REQUIRE (presets::canHavePresets (plugin));
        CHECK_FALSE (presets::canReset (plugin));
        CHECK (presets::getPluginName (plugin) == test::plugins::synth().name);

        level.setValueNotifyingHost (0.25f);
        REQUIRE (presets::save (plugin, "Quiet", presetsFolder.folder).isEmpty());
        level.setValueNotifyingHost (0.75f);

        const auto files = presets::list (plugin, presetsFolder.folder);
        REQUIRE (files.size() == 1);
        REQUIRE (presets::load (plugin, files[0]).isEmpty());
        CHECK (waitFor ([&] { return std::abs (level.getValue() - 0.25f) < 1.0e-4f; }));

        song.project->undo();
        CHECK (waitFor ([&] { return std::abs (level.getValue() - 0.75f) < 1.0e-4f; }));

        song.project->redo();
        CHECK (waitFor ([&] { return std::abs (level.getValue() - 0.25f) < 1.0e-4f; }));

        // The song's copy of the settings is the preset's: the plugin set up again starts from it.
        juce::MemoryBlock inSong;
        inSong.fromBase64Encoding (plugin.state[te::IDs::state].toString());
        const auto xml = juce::AudioProcessor::getXmlFromBinary (inSong.getData(), (int) inSong.getSize());
        REQUIRE (xml != nullptr);
        CHECK (xml->getDoubleAttribute ("level") == doctest::Approx (0.25));
    }

    TEST_CASE ("a plugin's own presets: listed, chosen as one undo step")
    {
        test::plugins::SynthSong song;
        auto& plugin = song.instrument();
        song.project->clearUndoHistory();

        CHECK (presets::getOwnPresets (plugin).joinIntoString ("|") == "One|Two|Three");
        CHECK (presets::getCurrentOwnPreset (plugin) == 0);

        presets::useOwnPreset (plugin, 2);
        CHECK (presets::getCurrentOwnPreset (plugin) == 2);
        CHECK (song.project->getUndoDescription() == "Preset Three");

        song.project->undo();
        CHECK (presets::getCurrentOwnPreset (plugin) == 0);

        song.project->redo();
        CHECK (presets::getCurrentOwnPreset (plugin) == 2);

        // The built-in ones have none of their own.
        BuiltInSong builtIn;
        CHECK (presets::getOwnPresets (*audio::instruments::get (builtIn.synthTrack)).isEmpty());
    }
}
