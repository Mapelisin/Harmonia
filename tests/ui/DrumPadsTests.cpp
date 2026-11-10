#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "audio/DrumKit.h"
#include "audio/Instruments.h"
#include "core/Project.h"
#include "ui/DrumPadsPanel.h"
#include "ui/FileDrag.h"
#include "ui/MainComponent.h"
#include "ui/PluginWindow.h"

using namespace harmonia;
namespace drum_kit = audio::drum_kit;
namespace instruments = audio::instruments;

namespace
{
    constexpr int kick = 36, snare = 38, spare = 100;

    void dispatch (int ms = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    std::optional<juce::PopupMenu::Item> find (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text.startsWith (text))
                return it.getItem();

        return std::nullopt;
    }

    /** A song with a drum track ("Drums", the drum machine, a clip) and a synth track, and sound
        files to use. */
    struct DrumFixture
    {
        DrumFixture()
        {
            drumTrack = project->getTracks().getFirst();
            drumTrack->setName ("Drums");
            REQUIRE (instruments::useDrumKit (*drumTrack).isEmpty());
            drums = instruments::get (*drumTrack);
            REQUIRE (drums != nullptr);
            drumClip = project->addMidiClip (*drumTrack, 0.0, 4.0, "Beat");

            synthTrack = project->addTrack ("Keys");
            REQUIRE (instruments::useBuiltInSynth (*synthTrack).isEmpty());
            synthClip = project->addMidiClip (*synthTrack, 0.0, 4.0, "Keys");
            project->clearUndoHistory();
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* drumTrack = nullptr;
        te::AudioTrack* synthTrack = nullptr;
        te::Plugin* drums = nullptr;
        te::MidiClip* drumClip = nullptr;
        te::MidiClip* synthClip = nullptr;
        const juce::File myKick = test::writeTone (temp.getFile ("My kick.wav"), 0.3, 0.5f);
        const juce::File mySnare = test::writeTone (temp.getFile ("Snare 9.wav"), 0.2, 0.4f);
    };
}

TEST_SUITE ("ui: samples in the drum machine")
{
    TEST_CASE ("the drum machine's window: a drum per key; a file dropped on one becomes its sound, with volume and tuning")
    {
        DrumFixture f;
        ui::DrumPadsPanel panel (*f.drums);

        // The kit's knobs above, its drums below.
        CHECK (panel.getKnobs().getNumRows() == 3);
        CHECK (panel.getNumPads() > 20);
        const auto kickPad = panel.indexOfNote (kick);
        REQUIRE (kickPad >= 0);
        CHECK (panel.getPadName (kickPad) == "Kick");
        CHECK (panel.getPadSound (kickPad) == "Built-in");
        CHECK (panel.indexOfNote (spare) == -1);
        CHECK_FALSE (panel.getVolumeSlider (kickPad)->isEnabled());
        CHECK_FALSE (panel.getClearButton (kickPad)->isEnabled());
        CHECK (panel.createComponentSnapshot (panel.getLocalBounds()).isValid());

        panel.dropFile (kick, f.myKick);
        CHECK (panel.getPadSound (kickPad) == "My kick");

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("drum-pads.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (panel.createComponentSnapshot (panel.getLocalBounds()), out);
        }
        CHECK (panel.getMessage() == "Kick plays My kick");
        CHECK (drum_kit::getSample (*f.drums, kick)->file == f.myKick);
        CHECK (panel.getVolumeSlider (kickPad)->isEnabled());
        CHECK (f.project->getUndoDescription() == "Sample for Kick");

        // Its own volume and tuning: one undo step per drag.
        auto& volume = *panel.getVolumeSlider (kickPad);
        volume.onDragStart();
        volume.setValue (-6.0, juce::sendNotificationSync);
        volume.setValue (-9.0, juce::sendNotificationSync);
        volume.onDragEnd();
        CHECK (drum_kit::getSample (*f.drums, kick)->gainDb == doctest::Approx (-9.0f));
        CHECK (f.project->getUndoDescription() == "Sample volume");

        auto& tune = *panel.getTuneSlider (kickPad);
        tune.onDragStart();
        tune.setValue (-3.0, juce::sendNotificationSync);
        tune.onDragEnd();
        CHECK (drum_kit::getSample (*f.drums, kick)->tune == doctest::Approx (-3.0f));

        f.project->undo();
        f.project->undo();
        panel.refresh();
        CHECK (drum_kit::getSample (*f.drums, kick)->gainDb == doctest::Approx (0.0f));
        CHECK (volume.getValue() == doctest::Approx (0.0));
        CHECK (tune.getValue() == doctest::Approx (0.0));

        // What isn't a sound: said so, nothing changed.
        const auto text = f.temp.getFile ("Notes.txt");
        text.replaceWithText ("words");
        panel.dropFile (snare, text);
        CHECK (panel.getMessage().contains ("Notes.txt"));
        CHECK (panel.getPadSound (panel.indexOfNote (snare)) == "Built-in");

        // The cross: the kit's own sound again.
        panel.getClearButton (kickPad)->onClick();
        CHECK (panel.getPadSound (kickPad) == "Built-in");
        CHECK_FALSE (drum_kit::getSample (*f.drums, kick).has_value());

        // Undo brings the sample back; the window follows by itself.
        f.project->undo();
        panel.refresh();
        CHECK (panel.getPadSound (kickPad) == "My kick");

        // A key the kit has no sound for gets a drum of its own once it has a sample.
        REQUIRE (drum_kit::setSample (*f.drums, spare, f.mySnare).isEmpty());
        panel.refresh();
        REQUIRE (panel.indexOfNote (spare) >= 0);
        CHECK (panel.getPadSound (panel.indexOfNote (spare)) == "Snare 9");

        // The file gone: the row says so.
        f.mySnare.deleteFile();
        panel.refresh();
        CHECK (panel.getPadSound (panel.indexOfNote (spare)) == "Missing: Snare 9.wav");
    }

    TEST_CASE ("the drum machine opens that window; other built-in plugins the plain knobs")
    {
        DrumFixture f;
        ui::PluginWindow drumWindow (*f.drums);
        CHECK (drumWindow.showsKnobs());
        CHECK (dynamic_cast<ui::DrumPadsPanel*> (drumWindow.getKnobs()) != nullptr);

        ui::PluginWindow synthWindow (*instruments::get (*f.synthTrack));
        CHECK (dynamic_cast<ui::DrumPadsPanel*> (synthWindow.getKnobs()) == nullptr);
        CHECK (dynamic_cast<ui::ParameterPanel*> (synthWindow.getKnobs()) != nullptr);
    }

    TEST_CASE ("piano roll: a sound dropped on a key of a drum track becomes that key's sound and its name")
    {
        DrumFixture f;
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& model = main->getPianoRollModel();
        auto& keys = main->getPianoRoll().getKeys();
        auto& view = main->getPianoRoll().getView();
        main->getArrangementModel().openClip (f.drumClip);
        dispatch();

        REQUIRE (model.getDrumMachine() == f.drums);
        CHECK (model.getDrumName (kick) == "Kick");
        CHECK (keys.isInterestedInFileDrag ({ f.myKick.getFullPathName() }));

        // Dragged over the kick's row, then dropped.
        const auto y = juce::roundToInt (view.pitchToY (kick) + view.rowHeight * 0.5f);
        keys.fileDragMove ({ f.myKick.getFullPathName() }, 10, y);
        CHECK (keys.getDropPitch() == kick);
        keys.filesDropped ({ f.myKick.getFullPathName() }, 10, y);
        CHECK (keys.getDropPitch() == -1);
        CHECK (model.hasDrumSample (kick));
        CHECK (model.getDrumName (kick) == "My kick");
        CHECK (drum_kit::getSample (*f.drums, kick)->file == f.myKick);

        // From the sound browser: the same, by the drag it starts.
        const juce::DragAndDropTarget::SourceDetails drag (ui::file_drag::describe ({ f.mySnare.getFullPathName() }), nullptr,
                                                           { 10, juce::roundToInt (view.pitchToY (snare) + view.rowHeight * 0.5f) });
        CHECK (keys.isInterestedInDragSource (drag));
        keys.itemDropped (drag);
        CHECK (model.getDrumName (snare) == "Snare 9");

        // The key's menu puts the kit's own sound back.
        const auto menu = keys.createMenu (kick);
        CHECK (find (menu, "Choose a sound file for it...").has_value());
        REQUIRE (find (menu, "Back to the kit's own sound").has_value());
        CHECK (find (menu, "Back to the kit's own sound")->isEnabled);
        find (menu, "Back to the kit's own sound")->action();
        CHECK_FALSE (model.hasDrumSample (kick));
        CHECK (model.getDrumName (kick) == "Kick");
        CHECK_FALSE (find (keys.createMenu (kick), "Back to the kit's own sound")->isEnabled);

        f.project->undo();
        CHECK (model.hasDrumSample (kick));

        // A track with another instrument: its keys don't take sounds.
        main->getArrangementModel().openClip (f.synthClip);
        dispatch();
        CHECK (model.getDrumMachine() == nullptr);
        CHECK_FALSE (keys.isInterestedInFileDrag ({ f.myKick.getFullPathName() }));
        CHECK (model.setDrumSample (kick, f.myKick).isNotEmpty());
        CHECK_FALSE (find (keys.createMenu (kick), "Choose a sound file").has_value());

        main.reset();
    }

    TEST_CASE ("sound browser: a sound's menu puts it on a drum of the current drum track")
    {
        DrumFixture f;
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& browser = main->getSoundBrowser();
        browser.setFolder (f.temp.folder);
        const auto row = browser.indexOf (f.myKick);
        REQUIRE (row >= 0);

        main->getArrangementModel().setCurrentTrack (f.drumTrack);
        dispatch();
        auto menu = browser.createMenu (row);
        REQUIRE (find (menu, "Use as a drum sound of Drums").has_value());
        REQUIRE (find (menu, "Snare (").has_value());
        find (menu, "Snare (")->action();
        REQUIRE (drum_kit::getSample (*f.drums, snare).has_value());
        CHECK (drum_kit::getSample (*f.drums, snare)->file == f.myKick);

        // The drum that has a sample is ticked.
        CHECK (find (browser.createMenu (row), "Snare (")->isTicked);

        f.project->undo();
        CHECK_FALSE (drum_kit::getSample (*f.drums, snare).has_value());

        // Another instrument on the current track: nothing about drums in the menu.
        main->getArrangementModel().setCurrentTrack (f.synthTrack);
        dispatch();
        CHECK_FALSE (find (browser.createMenu (row), "Use as a drum sound").has_value());

        main.reset();
    }
}
