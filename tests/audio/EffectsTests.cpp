#include <doctest.h>

#include "TestSession.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/PluginControls.h"
#include "core/Project.h"

using namespace harmonia;
namespace effects = audio::effects;

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

    juce::StringArray namesOf (te::PluginList& chain)
    {
        juce::StringArray names;

        for (auto* plugin : effects::get (chain))
            names.add (effects::getDisplayName (*plugin));

        return names;
    }

    juce::StringArray list (std::initializer_list<const char*> names)
    {
        juce::StringArray result;

        for (const auto* name : names)
            result.add (name);

        return result;
    }

    /** A track with the built-in synth, no undo history. */
    struct SynthTrack
    {
        SynthTrack()
            : project (core::Project::createEmpty (test::session())),
              track (*project->getTracks().getFirst())
        {
            REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());
            project->clearUndoHistory();
        }

        te::PluginList& chain()             { return track.pluginList; }
        te::PluginList& master()            { return project->getEdit().getMasterPluginList(); }
        juce::UndoManager& undoManager()    { return project->getEdit().getUndoManager(); }

        te::Plugin* add (const juce::String& name, int position = -1)
        {
            return addTo (chain(), name, position);
        }

        te::Plugin* addTo (te::PluginList& target, const juce::String& name, int position = -1)
        {
            juce::String error;
            auto* plugin = effects::add (target, builtIn (name), error, position);
            REQUIRE_MESSAGE (plugin != nullptr, error.toStdString());
            CHECK (error.isEmpty());
            return plugin;
        }

        std::unique_ptr<core::Project> project;
        te::AudioTrack& track;
    };

    /** Two bars of a chord on the built-in synth. */
    void addChord (core::Project& project, te::AudioTrack& track)
    {
        auto* clip = project.addMidiClip (track, 0.0, 8.0, "Chord");
        REQUIRE (clip != nullptr);

        for (int pitch : { 60, 64, 67, 72 })
            clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (6.0), 110, 0, nullptr);
    }

    /** RMS of an exported song. */
    float exportRms (core::Project& project)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (project.getEdit(), file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        return buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    }
}

TEST_SUITE ("audio::effects")
{
    TEST_CASE ("effects go after the instrument and before the fader, where they are asked to")
    {
        SynthTrack t;
        CHECK (effects::get (t.chain()).isEmpty());

        auto* eq = t.add ("EQ");
        t.add ("Reverb");
        CHECK (namesOf (t.chain()) == list ({ "EQ", "Reverb" }));

        // The whole list: instrument, effects, then the fader and the meter.
        auto& chain = t.chain();
        CHECK (chain[0]->isSynth());
        CHECK (chain.indexOf (eq) == 1);
        CHECK (chain.indexOf (t.track.getVolumePlugin()) > chain.indexOf (effects::get (chain).getLast()));
        CHECK (chain.indexOf (t.track.getLevelMeterPlugin()) == chain.size() - 1);

        t.add ("Delay", 0);
        t.add ("Compressor", 1);
        CHECK (namesOf (t.chain()) == list ({ "Delay", "Compressor", "EQ", "Reverb" }));
        CHECK (effects::getChain (*eq) == &t.chain());
    }

    TEST_CASE ("every built-in effect can be added; each add is one undo step")
    {
        SynthTrack t;
        const auto all = effects::getBuiltIn();
        CHECK (all.size() == 9);

        for (const auto& type : all)
            t.add (type.name);

        CHECK (effects::get (t.chain()).size() == all.size());

        t.project->undo();
        CHECK (effects::get (t.chain()).size() == all.size() - 1);
        t.project->redo();
        CHECK (effects::get (t.chain()).size() == all.size());
    }

    TEST_CASE ("an effect moves up and down its chain, one undo step each")
    {
        SynthTrack t;
        auto* eq = t.add ("EQ");
        t.add ("Reverb");
        auto* delay = t.add ("Delay");

        CHECK (effects::move (*delay, 0));
        CHECK (namesOf (t.chain()) == list ({ "Delay", "EQ", "Reverb" }));

        CHECK (effects::move (*delay, 2));
        CHECK (namesOf (t.chain()) == list ({ "EQ", "Reverb", "Delay" }));

        CHECK (effects::move (*eq, 1));
        CHECK (namesOf (t.chain()) == list ({ "Reverb", "EQ", "Delay" }));

        // Out of range clamps; the same place is no move.
        CHECK (effects::move (*eq, 99));
        CHECK (namesOf (t.chain()) == list ({ "Reverb", "Delay", "EQ" }));
        CHECK_FALSE (effects::move (*eq, 2));

        t.project->undo();
        CHECK (namesOf (t.chain()) == list ({ "Reverb", "EQ", "Delay" }));
        t.project->undo();
        CHECK (namesOf (t.chain()) == list ({ "EQ", "Reverb", "Delay" }));
        t.project->redo();
        CHECK (namesOf (t.chain()) == list ({ "Reverb", "EQ", "Delay" }));

        // The fader is still after the effects.
        CHECK (t.chain().indexOf (t.track.getVolumePlugin()) > t.chain().indexOf (effects::get (t.chain()).getLast()));
    }

    TEST_CASE ("bypassing and removing are undoable")
    {
        SynthTrack t;
        auto* reverb = t.add ("Reverb");
        t.add ("Delay");

        effects::setBypassed (*reverb, true);
        CHECK (effects::isBypassed (*reverb));
        t.project->undo();
        CHECK_FALSE (effects::isBypassed (*reverb));
        t.project->redo();
        CHECK (effects::isBypassed (*reverb));

        effects::remove (*reverb);
        CHECK (namesOf (t.chain()) == list ({ "Delay" }));
        t.project->undo();
        CHECK (namesOf (t.chain()) == list ({ "Reverb", "Delay" }));
        CHECK (effects::isBypassed (*effects::get (t.chain()).getFirst()));
    }

    TEST_CASE ("changing the instrument keeps the effects")
    {
        SynthTrack t;
        t.add ("Reverb");
        t.add ("Delay");

        REQUIRE (audio::instruments::useBuiltInSynth (t.track).isEmpty());
        CHECK (t.chain()[0]->isSynth());
        CHECK (namesOf (t.chain()) == list ({ "Reverb", "Delay" }));
    }

    TEST_CASE ("the master takes more than Tracktion's default four effects")
    {
        SynthTrack t;

        for (const auto* name : { "EQ", "Compressor", "Reverb", "Delay", "Chorus", "Filter" })
            t.addTo (t.master(), name);

        CHECK (effects::get (t.master()).size() == 6);
        CHECK (effects::canAdd (t.master()));
        CHECK (effects::get (t.chain()).isEmpty());
    }

    TEST_CASE ("plugins that fail to load, or are instruments, are not added")
    {
        SynthTrack t;
        juce::String error;

        effects::EffectType missing;
        missing.name = "Missing Reverb";
        missing.description.name = missing.name;
        missing.description.pluginFormatName = "VST3";
        missing.description.fileOrIdentifier = "C:/does/not/exist/Missing Reverb.vst3";
        CHECK (effects::add (t.chain(), missing, error) == nullptr);
        CHECK (error.isNotEmpty());

        effects::EffectType synth;
        synth.name = "Some Synth";
        synth.description.isInstrument = true;
        CHECK (effects::add (t.chain(), synth, error) == nullptr);
        CHECK (error.contains ("instrument"));

        CHECK (effects::get (t.chain()).isEmpty());
    }

    TEST_CASE ("effects and their settings are saved with the project")
    {
        juce::TemporaryFile file (".harmonia");

        {
            SynthTrack t;
            auto* reverb = t.add ("Reverb");
            auto* filter = t.add ("Filter");
            t.addTo (t.master(), "Compressor");

            effects::setBypassed (*reverb, true);

            for (auto& control : audio::PluginControl::getAll (*filter))
                if (! control.getChoices().isEmpty())
                    control.setValue (1.0f);   // high-pass

            for (auto& control : audio::PluginControl::getAll (*reverb))
                if (control.getName() == "Room Size")
                    control.setValue (0.9f);

            REQUIRE (t.project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = core::Project::load (test::session(), file.getFile(), error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

        auto& chain = loaded->getTracks().getFirst()->pluginList;
        CHECK (namesOf (chain) == list ({ "Reverb", "High-pass" }));
        CHECK (namesOf (loaded->getEdit().getMasterPluginList()) == list ({ "Compressor" }));

        auto* reverb = effects::get (chain).getFirst();
        CHECK (effects::isBypassed (*reverb));

        for (auto& control : audio::PluginControl::getAll (*reverb))
            if (control.getName() == "Room Size")
                CHECK (control.getValue() == doctest::Approx (0.9f));
    }

    TEST_CASE ("a low-pass filter takes the sound down; bypassed it changes nothing")
    {
        SynthTrack t;
        addChord (*t.project, t.track);
        const auto dry = exportRms (*t.project);
        REQUIRE (dry > 0.001f);

        auto* filter = t.add ("Filter");

        for (auto& control : audio::PluginControl::getAll (*filter))
            if (control.getName() == "Frequency")
                control.setValue (80.0f);

        const auto filtered = exportRms (*t.project);
        CHECK (filtered < dry * 0.5f);

        effects::setBypassed (*filter, true);
        CHECK (exportRms (*t.project) == doctest::Approx (dry).epsilon (0.05));
    }

    TEST_CASE ("an effect on the master changes the export too")
    {
        SynthTrack t;
        addChord (*t.project, t.track);
        const auto dry = exportRms (*t.project);

        auto* filter = t.addTo (t.master(), "Filter");

        for (auto& control : audio::PluginControl::getAll (*filter))
            if (control.getName() == "Frequency")
                control.setValue (80.0f);

        CHECK (exportRms (*t.project) < dry * 0.5f);
    }
}

TEST_SUITE ("audio::PluginControl")
{
    TEST_CASE ("reverb's controls are its parameters; a change is one undo step")
    {
        SynthTrack t;
        auto* reverb = t.add ("Reverb");
        auto controls = audio::PluginControl::getAll (*reverb);

        juce::StringArray names;

        for (auto& control : controls)
            names.add (control.getName());

        CHECK (names.contains ("Room Size"));
        CHECK (names.contains ("Wet Level"));

        auto& room = controls[(size_t) names.indexOf ("Room Size")];
        const auto before = room.getValue();

        room.beginChange();
        room.setValue (0.2f);
        room.setValue (0.8f);
        room.endChange();
        CHECK (room.getValue() == doctest::Approx (0.8f));
        CHECK (room.getText (0.5f).isNotEmpty());

        t.project->undo();
        CHECK (room.getValue() == doctest::Approx (before));
    }

    TEST_CASE ("chorus and phaser settings are reachable and undoable")
    {
        SynthTrack t;
        auto* chorus = t.add ("Chorus");
        auto controls = audio::PluginControl::getAll (*chorus);
        REQUIRE (controls.size() >= 4);
        CHECK (controls[0].getName() == "Depth");
        CHECK (controls[0].getValue() == doctest::Approx (3.0f));

        controls[0].beginChange();
        controls[0].setValue (7.0f);
        controls[0].endChange();
        CHECK ((float) chorus->state[te::IDs::depthMs] == doctest::Approx (7.0f));
        CHECK (controls[0].getText (7.0f) == "7.00 ms");

        t.project->undo();
        CHECK (controls[0].getValue() == doctest::Approx (3.0f));

        auto* phaser = t.add ("Phaser");
        CHECK (audio::PluginControl::getAll (*phaser).size() >= 3);
    }

    TEST_CASE ("the filter type is a choice; the delay time is whole milliseconds")
    {
        SynthTrack t;
        auto* filter = t.add ("Filter");
        auto filterControls = audio::PluginControl::getAll (*filter);
        REQUIRE (! filterControls.empty());
        CHECK (filterControls[0].getChoices() == list ({ "Low-pass", "High-pass" }));
        CHECK (filterControls[0].getValue() == doctest::Approx (0.0f));

        filterControls[0].setValue (1.0f);
        CHECK (effects::getDisplayName (*filter) == "High-pass");
        CHECK (filterControls[0].getValue() == doctest::Approx (1.0f));

        auto* delay = t.add ("Delay");
        auto delayControls = audio::PluginControl::getAll (*delay);
        REQUIRE (! delayControls.empty());
        CHECK (delayControls[0].getName() == "Time");
        delayControls[0].setValue (333.4f);
        CHECK ((int) delay->state[te::IDs::length] == 333);
        CHECK (delayControls[0].getText (delayControls[0].getValue()) == "333 ms");
    }

    TEST_CASE ("the built-in synth has knobs")
    {
        SynthTrack t;
        auto* synth = audio::instruments::get (t.track);
        REQUIRE (synth != nullptr);
        CHECK (audio::PluginControl::getAll (*synth).size() > 10);
    }
}
