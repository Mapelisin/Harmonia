#include <doctest.h>

#include "TestSession.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "audio/PluginControls.h"
#include "core/Project.h"
#include "ui/MainComponent.h"
#include "ui/ParameterPanel.h"
#include "ui/mixer/Mixer.h"

using namespace harmonia;
using namespace harmonia::ui::mixer;
namespace effects = audio::effects;

namespace
{
    effects::EffectType builtIn (const juce::String& name)
    {
        for (const auto& type : effects::getBuiltIn())
            if (type.name == name)
                return type;

        return {};
    }

    void dispatch()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    /** A project with one synth track and a mixer showing it. */
    struct MixerFixture
    {
        MixerFixture()
        {
            auto& track = *project->getTracks().getFirst();
            audio::instruments::useBuiltInSynth (track);
            project->addMidiClip (track, 0.0, 16.0, "Main");
            project->clearUndoHistory();

            mixer = std::make_unique<Mixer> (test::session(), model);
            mixer->setBounds (0, 0, 900, 400);
            mixer->setVisible (true);
            mixer->onMessage = [this] (const juce::String& text, bool isProblem)
            {
                messages.add ((isProblem ? "! " : "") + text);
            };
        }

        ~MixerFixture()
        {
            mixer.reset();
        }

        te::AudioTrack& firstTrack() const      { return *project->getTracks().getFirst(); }
        juce::UndoManager& undoManager()        { return project->getEdit().getUndoManager(); }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        ui::arrangement::ArrangementModel model { *project };
        std::unique_ptr<Mixer> mixer;
        juce::StringArray messages;
    };

    /** A slider moved by hand: drag start, values, drag end. */
    void drag (juce::Slider& slider, double value)
    {
        if (slider.onDragStart != nullptr)
            slider.onDragStart();

        slider.setValue (value, juce::sendNotificationSync);

        if (slider.onDragEnd != nullptr)
            slider.onDragEnd();
    }
}

TEST_SUITE ("mixer")
{
    TEST_CASE ("a strip per track and the master; strips follow tracks coming and going")
    {
        MixerFixture f;
        CHECK (f.mixer->getNumTrackStrips() == 1);
        CHECK (f.mixer->getTrackStrip (0)->getTrack() == &f.firstTrack());
        CHECK (f.mixer->getMasterStrip().isMaster());

        auto* added = f.model.addTrack();
        REQUIRE (added != nullptr);
        dispatch();
        CHECK (f.mixer->getNumTrackStrips() == 2);
        CHECK (f.mixer->getTrackStrip (1)->getTrack() == added);

        f.project->undo();
        dispatch();
        CHECK (f.mixer->getNumTrackStrips() == 1);

        // Painting with no device (no master levels) works.
        CHECK (f.mixer->createComponentSnapshot (f.mixer->getLocalBounds()).isValid());
    }

    TEST_CASE ("effects added from a strip show as slots; undo and removal follow")
    {
        MixerFixture f;
        auto& strip = *f.mixer->getTrackStrip (0);
        CHECK (strip.getNumSlots() == 0);

        strip.addEffect (builtIn ("Reverb"));
        strip.addEffect (builtIn ("Delay"));
        CHECK (strip.getNumSlots() == 2);
        CHECK (f.messages.size() == 2);
        CHECK_FALSE (f.messages[0].startsWith ("!"));

        f.project->undo();
        dispatch();
        CHECK (strip.getNumSlots() == 1);

        effects::remove (*effects::get (f.firstTrack().pluginList).getFirst());
        dispatch();
        CHECK (strip.getNumSlots() == 0);

        // The master strip has its own chain.
        f.mixer->getMasterStrip().addEffect (builtIn ("EQ"));
        CHECK (f.mixer->getMasterStrip().getNumSlots() == 1);
        CHECK (effects::get (f.project->getEdit().getMasterPluginList()).size() == 1);
        CHECK (strip.getNumSlots() == 0);
    }

    TEST_CASE ("a plugin that fails to load is reported, not added")
    {
        MixerFixture f;
        effects::EffectType missing;
        missing.name = "Missing";
        missing.description.name = "Missing";
        missing.description.pluginFormatName = "VST3";
        missing.description.fileOrIdentifier = "C:/does/not/exist/Missing.vst3";

        f.mixer->getTrackStrip (0)->addEffect (missing);
        CHECK (f.mixer->getTrackStrip (0)->getNumSlots() == 0);
        REQUIRE (f.messages.size() == 1);
        CHECK (f.messages[0].startsWith ("!"));
    }

    TEST_CASE ("fader and pan knob set the track's volume and pan, one undo step each")
    {
        MixerFixture f;
        auto& strip = *f.mixer->getTrackStrip (0);
        auto* volume = f.firstTrack().getVolumePlugin();
        REQUIRE (volume != nullptr);
        const auto startDb = volume->getVolumeDb();

        drag (strip.getFader(), (double) te::decibelsToVolumeFaderPosition (-12.0f));
        CHECK (volume->getVolumeDb() == doctest::Approx (-12.0f).epsilon (0.01));

        drag (strip.getPanKnob(), -0.5);
        CHECK (volume->getPan() == doctest::Approx (-0.5f));

        f.project->undo();
        CHECK (volume->getPan() == doctest::Approx (0.0f));
        CHECK (volume->getVolumeDb() == doctest::Approx (-12.0f).epsilon (0.01));
        f.project->undo();
        CHECK (volume->getVolumeDb() == doctest::Approx (startDb));

        // Redo brings both back (undoing to a default must not lose the redo steps).
        f.project->redo();
        CHECK (volume->getVolumeDb() == doctest::Approx (-12.0f).epsilon (0.01));
        f.project->redo();
        CHECK (volume->getPan() == doctest::Approx (-0.5f));
        f.project->undo();
        f.project->undo();

        // The strip shows changes made elsewhere.
        volume->setVolumeDb (-6.0f);
        dispatch();
        CHECK (strip.getFader().getValue() == doctest::Approx (volume->getSliderPos()));
    }

    TEST_CASE ("the master strip sets the master volume")
    {
        MixerFixture f;
        auto masterVolume = f.project->getEdit().getMasterVolumePlugin();
        REQUIRE (masterVolume != nullptr);

        drag (f.mixer->getMasterStrip().getFader(), (double) te::decibelsToVolumeFaderPosition (-3.0f));
        CHECK (masterVolume->getVolumeDb() == doctest::Approx (-3.0f).epsilon (0.01));
        CHECK (f.firstTrack().getVolumePlugin()->getVolumeDb() == doctest::Approx (0.0f));
    }

    TEST_CASE ("the level meter shows what its measurer heard, falls back and remembers clipping")
    {
        auto measurer = std::make_unique<te::LevelMeasurer>();
        te::LevelMeasurer* current = measurer.get();
        LevelMeter meter ([&current] { return current; });

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        buffer.setSample (0, 100, 0.5f);
        buffer.setSample (1, 100, 0.25f);
        measurer->processBuffer (buffer, 0, buffer.getNumSamples());
        meter.update();

        CHECK (meter.getLevelDb (0) == doctest::Approx (-6.02f).epsilon (0.02));
        CHECK (meter.getLevelDb (1) == doctest::Approx (-12.04f).epsilon (0.02));
        CHECK_FALSE (meter.hasClipped());

        buffer.setSample (0, 200, 1.5f);
        measurer->processBuffer (buffer, 0, buffer.getNumSamples());
        meter.update();
        CHECK (meter.hasClipped());

        // Silence: the bars fall, not jump.
        buffer.clear();
        measurer->processBuffer (buffer, 0, buffer.getNumSamples());
        juce::Thread::sleep (100);
        meter.update();
        CHECK (meter.getLevelDb (0) < 3.0f);
        CHECK (meter.getLevelDb (0) > LevelMeter::minDb);

        // The measurer goes away (e.g. the device restarted): no crash, nothing shown.
        current = nullptr;
        measurer.reset();
        meter.update();
        meter.setSize (12, 100);
        CHECK (meter.createComponentSnapshot (meter.getLocalBounds()).isValid());
    }
}

TEST_SUITE ("ParameterPanel")
{
    TEST_CASE ("a row per control, for effects and the built-in synth")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        juce::String error;
        auto* filter = effects::add (track.pluginList, builtIn ("Filter"), error);
        REQUIRE (filter != nullptr);

        ui::ParameterPanel filterPanel (*filter);
        CHECK (filterPanel.getNumRows() == (int) audio::PluginControl::getAll (*filter).size());
        CHECK (filterPanel.getHeight() == filterPanel.getIdealHeight());
        CHECK (filterPanel.createComponentSnapshot (filterPanel.getLocalBounds()).isValid());

        ui::ParameterPanel synthPanel (*audio::instruments::get (track));
        CHECK (synthPanel.getNumRows() > 10);
        CHECK (synthPanel.getHeight() < synthPanel.getIdealHeight());   // scrolls
    }
}

TEST_SUITE ("mixer: main window")
{
    struct MainFixture
    {
        MainFixture()
        {
            auto& track = *project->getTracks().getFirst();
            audio::instruments::useBuiltInSynth (track);
            project->addMidiClip (track, 0.0, 16.0, "Main");
            project->clearUndoHistory();
            main = std::make_unique<ui::MainComponent> (test::session(), *project);
            main->setBounds (0, 0, 1280, 800);
        }

        ~MainFixture()
        {
            main.reset();
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main;
    };

    TEST_CASE ("F9 shows the mixer in place of the piano roll, F7 brings the piano roll back")
    {
        using Panel = ui::MainComponent::LowerPanel;
        MainFixture f;
        CHECK (f.main->getLowerPanel() == Panel::pianoRoll);
        CHECK (f.main->getPianoRoll().isVisible());
        CHECK_FALSE (f.main->getMixer().isVisible());

        CHECK (f.main->keyPressed (juce::KeyPress (juce::KeyPress::F9Key)));
        CHECK (f.main->getLowerPanel() == Panel::mixer);
        CHECK (f.main->getMixer().isVisible());
        CHECK_FALSE (f.main->getPianoRoll().isVisible());
        CHECK (f.main->getMixer().getBounds() == f.main->getPianoRoll().getBounds());

        // Note-editing keys don't reach the hidden piano roll; undo still works.
        CHECK_FALSE (f.main->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));

        CHECK (f.main->keyPressed (juce::KeyPress (juce::KeyPress::F7Key)));
        CHECK (f.main->getLowerPanel() == Panel::pianoRoll);
        CHECK (f.main->getPianoRoll().isVisible());
    }

    TEST_CASE ("the hidden mixer catches up with tracks added meanwhile")
    {
        MainFixture f;
        f.main->getArrangementModel().addTrack();
        dispatch();

        f.main->showLowerPanel (ui::MainComponent::LowerPanel::mixer);
        CHECK (f.main->getMixer().getNumTrackStrips() == 2);
    }

    TEST_CASE ("the main window paints the mixer")
    {
        MainFixture f;
        auto& arrangement = f.main->getArrangementModel();

        for (const auto* name : { "Bass", "Lead", "Pad" })
            arrangement.renameTrack (*arrangement.addTrack(), name);

        auto tracks = arrangement.getTracks();
        juce::String error;

        for (const auto* name : { "EQ", "Compressor", "Reverb" })
            effects::add (tracks[0]->pluginList, builtIn (name), error);

        effects::add (tracks[1]->pluginList, builtIn ("Filter"), error);
        effects::setBypassed (*effects::add (tracks[2]->pluginList, builtIn ("Chorus"), error), true);
        effects::add (tracks[3]->pluginList, builtIn ("Delay"), error);
        effects::add (f.project->getEdit().getMasterPluginList(), builtIn ("Compressor"), error);

        tracks[1]->getVolumePlugin()->setVolumeDb (-8.0f);
        tracks[2]->getVolumePlugin()->setPan (-0.4f);
        tracks[3]->setMute (true);
        arrangement.setCurrentTrack (tracks[1]);

        f.main->showLowerPanel (ui::MainComponent::LowerPanel::mixer);
        dispatch();
        CHECK (f.main->getMixer().getNumTrackStrips() == 4);
        CHECK (f.main->getMixer().getTrackStrip (0)->getNumSlots() == 3);

        const auto image = f.main->createComponentSnapshot (f.main->getLocalBounds());
        CHECK (image.isValid());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-mixer.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
