#include "sandbox/TestPluginSong.h"

#include "audio/EffectChains.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "audio/PluginControls.h"

using namespace harmonia;
namespace effects = audio::effects;
namespace chains = audio::chains;

namespace
{
    effects::EffectType builtIn (const juce::String& name)
    {
        for (const auto& type : effects::getBuiltIn())
            if (type.name == name)
                return type;

        FAIL_CHECK ("no built-in effect called " << name.toStdString());
        return {};
    }

    juce::String namesOf (te::PluginList& chain)
    {
        juce::StringArray names;

        for (auto* plugin : effects::get (chain))
            names.add (effects::getDisplayName (*plugin));

        return names.joinIntoString (", ");
    }

    te::Plugin& add (te::PluginList& chain, const juce::String& name)
    {
        juce::String error;
        auto* plugin = effects::add (chain, builtIn (name), error);
        REQUIRE_MESSAGE (plugin != nullptr, error.toStdString());
        return *plugin;
    }

    /** The first slider of a plugin. */
    audio::PluginControl firstKnob (te::Plugin& plugin)
    {
        for (auto& control : audio::PluginControl::getAll (plugin))
            if (control.getChoices().isEmpty())
                return control;

        FAIL ("no knob");
        return audio::PluginControl::getAll (plugin).front();
    }

    void turn (te::Plugin& plugin, float fraction)
    {
        auto knob = firstKnob (plugin);
        knob.setValue (knob.getRange().convertFrom0to1 (fraction));
    }

    float turnOf (te::Plugin& plugin)
    {
        auto knob = firstKnob (plugin);
        return knob.getRange().convertTo0to1 (knob.getValue());
    }

    struct ChainsFolder
    {
        ChainsFolder()      { folder.createDirectory(); }
        ~ChainsFolder()     { folder.deleteRecursively(); }

        const juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getNonexistentChildFile ("harmonia-chains", {});
    };

    template <typename Condition>
    bool waitFor (Condition&& condition, int timeoutMs = 5000)
    {
        for (const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
             ! condition() && juce::Time::getMillisecondCounter() < until;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        return condition();
    }
}

TEST_SUITE ("audio::effect chains")
{
    TEST_CASE ("an effect moves to another track's chain or the master's with its settings, one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& first = *project->getTracks().getFirst();
        auto& second = *project->addTrack ("Second");
        REQUIRE (audio::instruments::useBuiltInSynth (first).isEmpty());

        add (first.pluginList, "Reverb");
        auto& delay = add (first.pluginList, "Delay");
        add (second.pluginList, "EQ");
        add (second.pluginList, "Chorus");
        turn (delay, 0.8f);
        const auto delayID = delay.itemID;
        project->clearUndoHistory();

        juce::String error;
        REQUIRE (effects::moveTo (delay, second.pluginList, 1, error));
        CHECK (error.isEmpty());
        CHECK (namesOf (first.pluginList) == "Reverb");
        CHECK (namesOf (second.pluginList) == "EQ, Delay, Chorus");
        CHECK (delay.itemID == delayID);        // the same effect, not a copy
        CHECK (turnOf (delay) == doctest::Approx (0.8f).epsilon (0.01));
        CHECK (project->getUndoDescription() == "Move Delay");

        // It still comes before the fader there.
        CHECK (second.pluginList.indexOf (&delay) < second.pluginList.indexOf (second.getVolumePlugin()));

        project->undo();
        CHECK (namesOf (first.pluginList) == "Reverb, Delay");
        CHECK (namesOf (second.pluginList) == "EQ, Chorus");
        project->redo();
        CHECK (namesOf (second.pluginList) == "EQ, Delay, Chorus");

        // After the others; onto the master.
        auto* moved = effects::get (second.pluginList)[1];
        REQUIRE (effects::moveTo (*moved, second.pluginList, -1, error));
        CHECK (namesOf (second.pluginList) == "EQ, Chorus, Delay");

        auto& master = project->getEdit().getMasterPluginList();
        REQUIRE (effects::moveTo (*effects::get (second.pluginList)[2], master, -1, error));
        CHECK (namesOf (master) == "Delay");
        CHECK (namesOf (second.pluginList) == "EQ, Chorus");

        // The instrument isn't an effect to move.
        CHECK_FALSE (effects::moveTo (*audio::instruments::get (first), second.pluginList, 0, error));
        CHECK (error.isNotEmpty());
    }

    TEST_CASE ("a copy of an effect has its settings and bypass, not its automation; one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& first = *project->getTracks().getFirst();
        auto& second = *project->addTrack ("Second");

        auto& reverb = add (first.pluginList, "Reverb");
        turn (reverb, 0.9f);
        effects::setBypassed (reverb, true);
        add (second.pluginList, "EQ");
        project->clearUndoHistory();

        juce::String error;
        auto* copy = effects::copyTo (reverb, second.pluginList, 0, error);
        REQUIRE_MESSAGE (copy != nullptr, error.toStdString());
        CHECK (copy != &reverb);
        CHECK (copy->itemID != reverb.itemID);
        CHECK (namesOf (first.pluginList) == "Reverb");
        CHECK (namesOf (second.pluginList) == "Reverb, EQ");
        CHECK (turnOf (*copy) == doctest::Approx (0.9f).epsilon (0.01));
        CHECK (effects::isBypassed (*copy));
        CHECK (project->getUndoDescription() == "Copy Reverb");

        // Its own from then on.
        turn (*copy, 0.1f);
        CHECK (turnOf (reverb) == doctest::Approx (0.9f).epsilon (0.01));

        project->undo();
        project->undo();
        CHECK (namesOf (second.pluginList) == "EQ");

        // On the same track: a second one.
        REQUIRE (effects::copyTo (reverb, first.pluginList, -1, error) != nullptr);
        CHECK (namesOf (first.pluginList) == "Reverb, Reverb");
    }

    TEST_CASE ("a track takes many more effects than Tracktion's sixteen plugins")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        for (int i = 0; i < 30; ++i)
            add (track.pluginList, i % 2 == 0 ? "EQ" : "Compressor");

        CHECK (effects::get (track.pluginList).size() == 30);
        CHECK (effects::canAdd (track.pluginList));
    }

    TEST_CASE ("a chain is saved to a file and loaded onto another track: after its effects or in place of them")
    {
        ChainsFolder chainsFolder;
        auto project = core::Project::createEmpty (test::session());
        auto& first = *project->getTracks().getFirst();
        auto& second = *project->addTrack ("Second");

        CHECK (chains::save (first.pluginList, "Empty", chainsFolder.folder).isNotEmpty());     // nothing to save

        add (first.pluginList, "EQ");
        auto& compressor = add (first.pluginList, "Compressor");
        auto& reverb = add (first.pluginList, "Reverb");
        turn (compressor, 0.7f);
        effects::setBypassed (reverb, true);

        CHECK (chains::save (first.pluginList, "  ", chainsFolder.folder).isNotEmpty());
        REQUIRE (chains::save (first.pluginList, "Vocal: bright?", chainsFolder.folder).isEmpty());
        REQUIRE (chains::save (first.pluginList, "A 10", chainsFolder.folder).isEmpty());
        REQUIRE (chains::save (first.pluginList, "A 9", chainsFolder.folder).isEmpty());

        const auto files = chains::list (chainsFolder.folder);
        REQUIRE (files.size() == 3);
        CHECK (chains::getName (files[0]) == "A 9");
        CHECK (chains::getName (files[1]) == "A 10");
        CHECK (files[2] == chains::getFileFor ("Vocal: bright?", chainsFolder.folder));
        CHECK (files[2].hasFileExtension (chains::fileExtension));

        // Saving changes nothing in the song.
        CHECK (namesOf (first.pluginList) == "EQ, Compressor, Reverb");

        add (second.pluginList, "Delay");
        project->clearUndoHistory();

        auto loaded = chains::load (second.pluginList, files[2], false);
        CHECK (loaded.error.isEmpty());
        CHECK (loaded.problems.isEmpty());
        CHECK (loaded.added == 3);
        CHECK (namesOf (second.pluginList) == "Delay, EQ, Compressor, Reverb");

        const auto onSecond = effects::get (second.pluginList);
        CHECK (turnOf (*onSecond[2]) == doctest::Approx (0.7f).epsilon (0.01));
        CHECK (effects::isBypassed (*onSecond[3]));
        CHECK_FALSE (effects::isBypassed (*onSecond[1]));
        CHECK (project->getUndoDescription() == "Chain " + chains::getName (files[2]));

        project->undo();
        CHECK (namesOf (second.pluginList) == "Delay");
        CHECK_FALSE (project->canUndo());

        loaded = chains::load (second.pluginList, files[0], true);
        CHECK (loaded.added == 3);
        CHECK (namesOf (second.pluginList) == "EQ, Compressor, Reverb");
        project->undo();
        CHECK (namesOf (second.pluginList) == "Delay");

        // The master takes a chain too.
        loaded = chains::load (project->getEdit().getMasterPluginList(), files[0], false);
        CHECK (loaded.added == 3);

        // Not a chain.
        const auto other = chainsFolder.folder.getChildFile ("Notes.harmoniachain");
        other.replaceWithText ("<NOTES/>");
        loaded = chains::load (second.pluginList, other, true);
        CHECK (loaded.error.isNotEmpty());
        CHECK (loaded.added == 0);
        CHECK (namesOf (second.pluginList) == "Delay");
    }

    TEST_CASE ("a plugin in a process of its own is copied with the settings made in its window")
    {
        test::plugins::SynthSong song;
        auto& track = song.track;
        auto& other = *song.project->addTrack ("Other");

        juce::String error;
        auto* gain = effects::add (track.pluginList, { test::plugins::gain().name, {}, test::plugins::gain() }, error);
        REQUIRE_MESSAGE (gain != nullptr, error.toStdString());

        auto* remote = audio::plugin_safety::getRemote (*gain);
        REQUIRE (remote != nullptr);
        auto* level = remote->getParameters()[test::plugins::level];
        level->setValueNotifyingHost (0.3f);

        auto* copy = effects::copyTo (*gain, other.pluginList, -1, error);
        REQUIRE_MESSAGE (copy != nullptr, error.toStdString());
        auto* copyRemote = audio::plugin_safety::getRemote (*copy);
        REQUIRE (copyRemote != nullptr);
        CHECK (copyRemote != remote);
        CHECK (waitFor ([&] { return std::abs (copyRemote->getParameters()[test::plugins::level]->getValue() - 0.3f) < 1.0e-3f; }));
    }
}
