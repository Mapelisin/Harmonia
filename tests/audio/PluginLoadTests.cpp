#include <doctest.h>

#include "TestAudioRig.h"
#include "TestSession.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "audio/PluginLoad.h"
#include "audio/Sends.h"
#include "audio/Transport.h"
#include "core/Project.h"
#include "ui/PluginLoadPanel.h"

using namespace harmonia;
namespace load = audio::plugin_load;
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

    /** A song with an instrument and two effects on "Lead", an effect on "Drums", one on a return
        track and one on the master, and made-up loads for some of them. */
    struct LoadedSong
    {
        LoadedSong()
        {
            lead.setName ("Lead");
            audio::instruments::useBuiltInSynth (lead);
            synth = audio::instruments::get (lead);
            REQUIRE (synth != nullptr);

            juce::String error;
            returnTrack = audio::sends::addReturnTrack (*project, audio::sends::Preset::reverb, error);
            REQUIRE_MESSAGE (returnTrack != nullptr, error.toStdString());
            REQUIRE (effects::get (returnTrack->pluginList).size() == 1);
            returnReverb = effects::get (returnTrack->pluginList).getFirst();

            loads = { { synth, 0.12 }, { &eq, 0.02 }, { &reverb, 0.30 }, { &limiter, 0.004 } };
        }

        load::Reader reader()
        {
            return [this] (te::Plugin& plugin)
            {
                const auto found = loads.find (&plugin);
                return found != loads.end() ? found->second : 0.0;
            };
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::Edit& edit = project->getEdit();
        te::AudioTrack& lead = *project->getTracks().getFirst();
        te::AudioTrack& drums = *project->addTrack ("Drums");
        te::Plugin& eq = addEffect (lead.pluginList, "EQ");
        te::Plugin& reverb = addEffect (lead.pluginList, "Reverb");
        te::Plugin& compressor = addEffect (drums.pluginList, "Compressor");
        te::Plugin& limiter = addEffect (edit.getMasterPluginList(), "Limiter");
        te::Plugin* synth = nullptr;
        te::AudioTrack* returnTrack = nullptr;
        te::Plugin* returnReverb = nullptr;
        std::map<te::Plugin*, double> loads;
    };
}

TEST_SUITE ("audio: each plugin's load")
{
    TEST_CASE ("the song's instruments and effects, each with whose it is, the heaviest first")
    {
        LoadedSong song;
        const auto entries = load::measure (song.edit, song.reader());

        // The instrument and the five effects: not the faders, the meters or the sends.
        REQUIRE (entries.size() == 6);
        CHECK (entries[0].plugin.get() == &song.reverb);
        CHECK (entries[0].name == "Reverb");
        CHECK (entries[0].owner == "Lead");
        CHECK (entries[0].load == doctest::Approx (0.30));
        CHECK (load::describe (entries[0]) == "Lead: Reverb");

        CHECK (entries[1].plugin.get() == song.synth);
        CHECK (entries[1].name == effects::getDisplayName (*song.synth));
        CHECK (entries[1].owner == "Lead");
        CHECK (entries[2].plugin.get() == &song.eq);

        CHECK (entries[3].plugin.get() == &song.limiter);
        CHECK (entries[3].owner == "Master");
        CHECK (load::describe (entries[3]) == "Master: Limiter");

        // Those that take nothing stay in the song's order: the tracks, then the return tracks.
        CHECK (entries[4].plugin.get() == &song.compressor);
        CHECK (entries[4].owner == "Drums");
        CHECK (entries[4].load == 0.0);
        CHECK (entries[5].plugin.get() == song.returnReverb);
        CHECK (entries[5].owner == song.returnTrack->getName());

        CHECK (load::getTotal (entries) == doctest::Approx (0.444));

        // The song's own order, when asked without loads.
        const auto all = effects::getAllOfSong (song.edit);
        REQUIRE (all.size() == 6);
        CHECK (all[0] == song.synth);
        CHECK (all[1] == &song.eq);
        CHECK (all[2] == &song.reverb);
        CHECK (all[3] == &song.compressor);
        CHECK (all[4] == song.returnReverb);
        CHECK (all[5] == &song.limiter);

        // A reader that says less than nothing is taken for nothing.
        for (const auto& entry : load::measure (song.edit, [] (te::Plugin&) { return -0.5; }))
            CHECK (entry.load == 0.0);
    }

    TEST_CASE ("a load is written in percent, small ones with a decimal")
    {
        CHECK (load::describe (0.0) == "0%");
        CHECK (load::describe (0.0004) == "0%");
        CHECK (load::describe (0.004) == "0.4%");
        CHECK (load::describe (0.0951) == "9.5%");
        CHECK (load::describe (0.0996) == "10%");
        CHECK (load::describe (0.30) == "30%");
        CHECK (load::describe (1.0) == "100%");
        CHECK (load::describe (-0.2) == "0%");
    }

    TEST_CASE ("the engine measures a playing instrument and its effect, and nothing for an effect that is switched off")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto& track = *rig.project->getTracks().getFirst();
        audio::instruments::useBuiltInSynth (track);
        auto& reverb = addEffect (track.pluginList, "Reverb");
        auto& off = addEffect (track.pluginList, "Delay");
        effects::setBypassed (off, true);

        auto* clip = rig.project->addMidiClip (track, 0.0, 16.0, "Chord");
        REQUIRE (clip != nullptr);

        for (int note : { 48, 55, 60, 64, 67, 72 })
            clip->getSequence().addNote (note, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (16.0), 100, 0, nullptr);

        edit.dispatchPendingUpdatesSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        audio::Transport transport (edit);
        transport.setPositionSeconds (0.0);
        transport.play();
        CHECK (rig.process (1.0, 0.0f) > 0.001f);

        const auto entries = load::measure (edit);
        REQUIRE (entries.size() == 3);
        auto* synth = audio::instruments::get (track);

        for (const auto& entry : entries)
        {
            INFO (entry.name.toStdString() << ": " << entry.load);

            if (entry.plugin.get() == &off)
            {
                CHECK (entry.load == 0.0);
            }
            else
            {
                CHECK ((entry.plugin.get() == synth || entry.plugin.get() == &reverb));
                CHECK (entry.load > 0.0);
                CHECK (entry.load <= 1.0);
            }
        }

        CHECK (entries.back().plugin.get() == &off);
        transport.stop();
        rig.process (0.2, 0.0f);
    }
}

TEST_SUITE ("ui: the list of each plugin's load")
{
    TEST_CASE ("the heaviest first, with the total; a click opens the plugin's window")
    {
        LoadedSong song;
        ui::PluginLoadPanel panel (song.edit, song.reader());

        REQUIRE (panel.getNumShown() == 6);
        CHECK (panel.getShownName (0) == "Lead: Reverb");
        CHECK (panel.getShownLoad (0) == "30%");
        CHECK (panel.getShownName (1) == "Lead: " + effects::getDisplayName (*song.synth));
        CHECK (panel.getShownLoad (1) == "12%");
        CHECK (panel.getShownName (3) == "Master: Limiter");
        CHECK (panel.getShownLoad (3) == "0.4%");
        CHECK (panel.getShownLoad (4) == "0%");
        CHECK (panel.getShownName (6).isEmpty());
        CHECK (panel.getSummary() == "Plugins take 44% of the audio time");
        CHECK (panel.getHeight() > 100);

        // It paints. Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            const auto file = juce::File (dir).getChildFile ("plugin-load.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            REQUIRE (out.openedOk());
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        // The loads change: read again, the order follows.
        song.loads[&song.compressor] = 0.5;
        panel.refresh();
        CHECK (panel.getShownName (0) == "Drums: Compressor");
        CHECK (panel.getSummary() == "Plugins take 94% of the audio time");

        te::Plugin* chosen = nullptr;
        panel.onChosen = [&chosen] (te::Plugin& plugin) { chosen = &plugin; };
        panel.choose (1);
        CHECK (chosen == &song.reverb);
        panel.choose (17);
        CHECK (chosen == &song.reverb);
    }

    TEST_CASE ("a song without plugins says so")
    {
        auto project = core::Project::createEmpty (test::session());
        ui::PluginLoadPanel panel (project->getEdit());
        CHECK (panel.getNumShown() == 0);
        CHECK (panel.getSummary() == "No instruments or effects in this song yet");
    }
}
