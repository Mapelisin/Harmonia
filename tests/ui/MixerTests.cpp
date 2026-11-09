#include <doctest.h>

#include "TestSession.h"
#include "audio/Effects.h"
#include "audio/Groups.h"
#include "audio/Instruments.h"
#include "audio/PluginControls.h"
#include "audio/Sends.h"
#include "audio/Sidechain.h"
#include "core/Project.h"
#include "ui/MainComponent.h"
#include "ui/ParameterPanel.h"
#include "ui/arrangement/TrackHeaders.h"
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

    /** The item at `path` ("Listen to (sidechain)/Kick": a submenu, then its item), or nullptr. */
    const juce::PopupMenu::Item* findItem (const juce::PopupMenu& menu, const juce::String& path)
    {
        const auto name = path.upToFirstOccurrenceOf ("/", false, false);
        const auto rest = path.fromFirstOccurrenceOf ("/", false, false);

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();

            if (item.text != name)
                continue;

            if (rest.isEmpty())
                return &item;

            return item.subMenu != nullptr ? findItem (*item.subMenu, rest) : nullptr;
        }

        return nullptr;
    }

    /** Picks the item at `path`, as a click on it would. */
    void choose (const juce::PopupMenu& menu, const juce::String& path)
    {
        const auto* item = findItem (menu, path);
        REQUIRE_MESSAGE (item != nullptr, path.toStdString());
        REQUIRE (item->isEnabled);
        REQUIRE (item->action != nullptr);
        item->action();
    }

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

TEST_SUITE ("mixer: sends")
{
    TEST_CASE ("return tracks get strips after the tracks; each song strip gets a send bar per return")
    {
        MixerFixture f;
        CHECK (f.mixer->getNumReturnStrips() == 0);
        CHECK (f.mixer->getTrackStrip (0)->getNumSends() == 0);

        auto* reverb = f.mixer->addReturnTrack (audio::sends::Preset::reverb);
        REQUIRE (reverb != nullptr);
        REQUIRE (f.messages.size() == 1);
        CHECK (f.messages[0].startsWith ("Return track Reverb added"));
        CHECK (f.mixer->getNumTrackStrips() == 1);
        CHECK (f.model.getTracks().size() == 1);   // not a song track: not in the arrangement

        REQUIRE (f.mixer->getNumReturnStrips() == 1);
        auto* returnStrip = f.mixer->getReturnStrip (0);
        auto* strip = f.mixer->getTrackStrip (0);
        CHECK (returnStrip->isReturn());
        CHECK_FALSE (strip->isReturn());
        CHECK (returnStrip->getTrack() == reverb);
        CHECK (returnStrip->getNumSlots() == 1);   // its reverb
        CHECK (returnStrip->getNumSends() == 0);
        CHECK (returnStrip->getX() > strip->getRight());

        REQUIRE (strip->getNumSends() == 1);
        CHECK (strip->getSendTarget (0) == reverb);

        // The faders line up: the return and the master keep room for the send rows too.
        CHECK (strip->getFader().getY() == returnStrip->getFader().getY());
        CHECK (strip->getFader().getY() == f.mixer->getMasterStrip().getFader().getY());

        drag (*strip->getSendSlider (0), 0.6);
        CHECK (audio::sends::getLevel (f.firstTrack(), *reverb) == doctest::Approx (0.6f));
        CHECK (f.undoManager().getUndoDescription() == "Send to Reverb");

        f.project->undo();
        dispatch();
        CHECK (audio::sends::getSend (f.firstTrack(), *reverb) == nullptr);
        REQUIRE (f.mixer->getNumTrackStrips() == 1);
        REQUIRE (f.mixer->getTrackStrip (0)->getNumSends() == 1);
        CHECK (f.mixer->getTrackStrip (0)->getSendSlider (0)->getValue() == 0.0);

        audio::sends::removeReturnTrack (*f.project, *reverb);
        dispatch();
        CHECK (f.mixer->getNumReturnStrips() == 0);
        CHECK (f.mixer->getTrackStrip (0)->getNumSends() == 0);

        f.project->undo();
        dispatch();
        CHECK (f.mixer->getNumReturnStrips() == 1);
        CHECK (f.mixer->getTrackStrip (0)->getNumSends() == 1);
    }

    TEST_CASE ("group tracks get strips between the tracks and the returns; a track's Output chooses its group")
    {
        MixerFixture f;
        auto* bass = f.model.addTrack();
        REQUIRE (bass != nullptr);
        REQUIRE (f.mixer->addReturnTrack (audio::sends::Preset::reverb) != nullptr);

        auto* group = f.mixer->addGroupTrack();
        REQUIRE (group != nullptr);
        CHECK (f.messages[f.messages.size() - 1].startsWith ("Group track Group added"));
        CHECK (f.model.getTracks().size() == 2);   // not in the arrangement
        REQUIRE (f.mixer->getNumTrackStrips() == 2);
        REQUIRE (f.mixer->getNumGroupStrips() == 1);
        REQUIRE (f.mixer->getNumReturnStrips() == 1);

        auto* groupStrip = f.mixer->getGroupStrip (0);
        auto* strip = f.mixer->getTrackStrip (0);
        CHECK (groupStrip->isGroup());
        CHECK (groupStrip->getTrack() == group);
        CHECK (groupStrip->getX() > f.mixer->getTrackStrip (1)->getRight());
        CHECK (f.mixer->getReturnStrip (0)->getX() > groupStrip->getRight());
        CHECK (groupStrip->getNumSends() == 1);   // a group can send to the reverb too
        CHECK (groupStrip->getFader().getY() == strip->getFader().getY());
        CHECK_FALSE (groupStrip->getOutputButton().isVisible());
        CHECK (strip->getOutputButton().isVisible());
        CHECK (strip->getOutputButton().getButtonText().endsWith ("Master"));

        // The Output menu: the master ticked, the group offered.
        auto menu = ui::arrangement::makeOutputMenu (f.model, f.firstTrack());
        REQUIRE (findItem (menu, "Master") != nullptr);
        CHECK (findItem (menu, "Master")->isTicked);
        choose (menu, "Group");
        dispatch();
        CHECK (audio::groups::getGroup (f.firstTrack()) == group);
        CHECK (f.undoManager().getUndoDescription() == "Play through Group");
        CHECK (f.mixer->getTrackStrip (0)->getOutputButton().getButtonText().endsWith ("Group"));
        CHECK (findItem (ui::arrangement::makeOutputMenu (f.model, f.firstTrack()), "Group")->isTicked);

        // Rendered to audio, it still plays through the group.
        REQUIRE (f.model.renderToAudio (f.firstTrack()).isEmpty());
        auto* rendered = f.model.getCurrentTrack();
        REQUIRE (rendered != nullptr);
        CHECK (rendered != &f.firstTrack());
        CHECK (audio::groups::getGroup (*rendered) == group);
        f.project->undo();
        dispatch();
        CHECK (f.model.getTracks().size() == 2);

        // A new group with the bass in it.
        choose (ui::arrangement::makeOutputMenu (f.model, *bass), "New group with this track");
        dispatch();
        REQUIRE (f.mixer->getNumGroupStrips() == 2);
        CHECK (audio::groups::getGroup (*bass) == f.mixer->getGroupStrip (1)->getTrack());
        CHECK (f.mixer->getGroupStrip (1)->getTrack()->getName() == "Group 2");

        f.project->undo();
        f.project->undo();
        dispatch();
        CHECK (audio::groups::getGroup (f.firstTrack()) == nullptr);
        CHECK (f.mixer->getNumGroupStrips() == 1);
        CHECK (f.mixer->getTrackStrip (0)->getOutputButton().getButtonText().endsWith ("Master"));

        // Deleted, the group takes no track with it.
        choose (ui::arrangement::makeOutputMenu (f.model, f.firstTrack()), "Group");
        audio::groups::removeGroup (*f.project, *group);
        dispatch();
        CHECK (f.mixer->getNumGroupStrips() == 0);
        CHECK (f.mixer->getNumTrackStrips() == 2);
        CHECK (f.mixer->getTrackStrip (0)->getOutputButton().getButtonText().endsWith ("Master"));
    }

    TEST_CASE ("an effect's slot menu sets what it listens to; deleting that track stops it")
    {
        MixerFixture f;
        auto* kick = f.model.addTrack();
        REQUIRE (kick != nullptr);
        kick->setName ("Kick");
        dispatch();

        auto* strip = f.mixer->getTrackStrip (0);
        strip->addEffect (builtIn ("Compressor"));
        REQUIRE (strip->getNumSlots() == 1);
        auto* compressor = effects::get (f.firstTrack().pluginList).getFirst();
        REQUIRE (compressor != nullptr);

        auto menu = strip->makeSlotMenu (*compressor);
        REQUIRE (findItem (menu, "Listen to (sidechain)/Nothing (its own track)") != nullptr);
        CHECK (findItem (menu, "Listen to (sidechain)/Nothing (its own track)")->isTicked);
        CHECK (findItem (menu, "Listen to (sidechain)/" + f.firstTrack().getName()) == nullptr);   // not itself
        choose (menu, "Listen to (sidechain)/Kick");
        CHECK (audio::sidechain::getSource (*compressor) == kick);
        CHECK (f.undoManager().getUndoDescription() == "Sidechain from Kick");
        CHECK (findItem (strip->makeSlotMenu (*compressor), "Listen to (sidechain)/Kick")->isTicked);

        // An effect without a sidechain input offers none.
        strip->addEffect (builtIn ("Reverb"));
        auto* reverb = effects::get (f.firstTrack().pluginList).getLast();
        CHECK (findItem (strip->makeSlotMenu (*reverb), "Listen to (sidechain)") == nullptr);

        f.model.deleteTrack (*kick);
        CHECK (audio::sidechain::getSource (*compressor) == nullptr);
        f.project->undo();
        CHECK (audio::sidechain::getSource (*compressor) != nullptr);
    }

    TEST_CASE ("tracks added after a return track come before it")
    {
        MixerFixture f;
        auto* delay = f.mixer->addReturnTrack (audio::sends::Preset::delay);
        REQUIRE (delay != nullptr);

        auto* added = f.model.addTrack();
        REQUIRE (added != nullptr);
        dispatch();
        CHECK (f.mixer->getNumTrackStrips() == 2);
        CHECK (f.mixer->getTrackStrip (1)->getTrack() == added);
        CHECK (f.mixer->getTrackStrip (1)->getNumSends() == 1);
        CHECK (f.mixer->getNumReturnStrips() == 1);
        CHECK (te::getAudioTracks (f.project->getEdit()).getLast() == delay);
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

        // A reverb and a delay shared by the tracks.
        auto* reverb = audio::sends::addReturnTrack (*f.project, audio::sends::Preset::reverb, error);
        auto* delay = audio::sends::addReturnTrack (*f.project, audio::sends::Preset::delay, error);
        REQUIRE (reverb != nullptr);
        REQUIRE (delay != nullptr);
        audio::sends::setLevelDb (*tracks[0], *reverb, -10.0f);
        audio::sends::setLevelDb (*tracks[2], *reverb, -4.0f);
        audio::sends::setLevelDb (*tracks[2], *delay, -14.0f);
        audio::sends::setLevelDb (*tracks[3], *delay, -2.0f);

        // Lead and Pad through one group; the bass's compressor listening to the first track.
        auto* synths = audio::groups::addGroup (*f.project, { tracks[2], tracks[3] }, "Synths");
        REQUIRE (synths != nullptr);
        effects::add (synths->pluginList, builtIn ("EQ"), error);
        synths->getVolumePlugin()->setVolumeDb (-3.0f);
        auto* bassCompressor = effects::add (tracks[1]->pluginList, builtIn ("Compressor"), error);
        REQUIRE (bassCompressor != nullptr);
        REQUIRE (audio::sidechain::setSource (*f.project, *bassCompressor, tracks[0]));

        f.main->showLowerPanel (ui::MainComponent::LowerPanel::mixer);
        dispatch();
        CHECK (f.main->getMixer().getNumTrackStrips() == 4);
        CHECK (f.main->getMixer().getTrackStrip (0)->getNumSlots() == 3);
        CHECK (f.main->getMixer().getNumGroupStrips() == 1);
        CHECK (f.main->getMixer().getNumReturnStrips() == 2);
        CHECK (f.main->getArrangement().getHeaders().getNumHeaders() == 4);   // return tracks stay out of the arrangement

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
