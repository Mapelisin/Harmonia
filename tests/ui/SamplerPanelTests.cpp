#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "audio/Instruments.h"
#include "audio/Sampler.h"
#include "core/Project.h"
#include "ui/FileDrag.h"
#include "ui/InstrumentPanel.h"
#include "ui/MainComponent.h"
#include "ui/PluginWindow.h"
#include "ui/SamplerPanel.h"
#include "ui/Theme.h"
#include "ui/arrangement/TrackHeaders.h"

using namespace harmonia;
namespace sampler = audio::sampler;
namespace instruments = audio::instruments;

namespace
{
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

    /** A song with a sampler track ("Sampler", a clip) and a synth track, and sound files. */
    struct SamplerFixture
    {
        SamplerFixture()
        {
            samplerTrack = project->getTracks().getFirst();
            samplerTrack->setName ("Sampler");
            REQUIRE (instruments::useSampler (*samplerTrack).isEmpty());
            instrument = instruments::get (*samplerTrack);
            REQUIRE (instrument != nullptr);
            samplerClip = project->addMidiClip (*samplerTrack, 0.0, 4.0, "Notes");

            synthTrack = project->addTrack ("Keys");
            REQUIRE (instruments::useBuiltInSynth (*synthTrack).isEmpty());
            synthClip = project->addMidiClip (*synthTrack, 0.0, 4.0, "Keys");

            // What the engine still does to new clips (it sorts them a moment later, with undo).
            dispatch();
            project->clearUndoHistory();
            text.replaceWithText ("words");
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* samplerTrack = nullptr;
        te::AudioTrack* synthTrack = nullptr;
        te::Plugin* instrument = nullptr;
        te::MidiClip* samplerClip = nullptr;
        te::MidiClip* synthClip = nullptr;
        const juce::File pluck = test::writeTone (temp.getFile ("Pluck.wav"), 2.0, 0.5f, 1, true);
        const juce::File pad = test::writeTone (temp.getFile ("Pad 7.wav"), 1.0, 0.4f, 2);
        const juce::File text = temp.getFile ("Notes.txt");
    };
}

TEST_SUITE ("ui: the sampler")
{
    TEST_CASE ("the sampler's window: knobs, the sound and its picture; a dropped file becomes the sound; the root key")
    {
        SamplerFixture f;
        ui::SamplerPanel panel (*f.instrument);
        panel.setSize (panel.getWidth(), panel.getHeight());

        CHECK (panel.getKnobs().getNumRows() == 5);
        CHECK (panel.getSoundText() == "Choose a sound...");
        CHECK (panel.getInfoText().isEmpty());
        CHECK (panel.getRootKey().getValue() == doctest::Approx (60.0));
        CHECK (panel.getRootKey().textFromValue (60.0) == "C4");

        // A sound dropped on it: one undo step, said, and heard for a moment.
        CHECK (panel.isInterestedInFileDrag ({ f.pluck.getFullPathName() }));
        panel.filesDropped ({ f.pluck.getFullPathName() }, 20, 200);
        CHECK (sampler::getFile (*f.instrument) == f.pluck);
        CHECK (f.project->getUndoDescription() == "Sampler: Pluck");
        CHECK (panel.getSoundText() == "Pluck");
        CHECK (panel.getInfoText() == "2.00 s, mono, 44.1 kHz");
        CHECK (panel.getMessage() == "Plays Pluck: C4 as it is");
        CHECK (panel.isAuditioning());
        panel.setAuditioning (false);
        CHECK_FALSE (panel.isAuditioning());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        {
            const auto image = panel.createComponentSnapshot (panel.getLocalBounds());

            if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
            {
                const auto file = juce::File (dir).getChildFile ("sampler.png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            // The picture of the sound is there: the accent colour somewhere under the sound's row.
            auto drawn = 0;

            for (int x = 0; x < image.getWidth(); x += 3)
                for (int y = panel.getHeight() - ui::SamplerPanel::pictureHeight; y < panel.getHeight(); y += 2)
                    if (image.getPixelAt (x, y) == ui::theme::colour::accent)
                        ++drawn;

            CHECK (drawn > 100);
        }

        // From the sound browser: the same, by the drag it starts.
        const juce::DragAndDropTarget::SourceDetails drag (ui::file_drag::describe ({ f.pad.getFullPathName() }), nullptr, { 20, 200 });
        CHECK (panel.isInterestedInDragSource (drag));
        panel.itemDropped (drag);
        panel.setAuditioning (false);
        CHECK (panel.getSoundText() == "Pad 7");
        CHECK (panel.getInfoText() == "1.00 s, stereo, 44.1 kHz");

        // What isn't a sound: said so, nothing changed.
        panel.dropFile (f.text);
        CHECK (panel.getMessage().contains ("Notes.txt"));
        CHECK (sampler::getFile (*f.instrument) == f.pad);
        CHECK_FALSE (panel.isAuditioning());

        // The root key: a turn of the wheel or a typed number is one undo step.
        panel.getRootKey().turnWheel (-12);
        CHECK (sampler::getRoot (*f.instrument) == 48);
        CHECK (f.project->getUndoDescription() == "Root key");
        panel.getRootKey().typeValue ("69");
        CHECK (sampler::getRoot (*f.instrument) == 69);
        f.project->undo();
        f.project->undo();
        panel.refresh();
        CHECK (sampler::getRoot (*f.instrument) == 60);
        CHECK (panel.getRootKey().getValue() == doctest::Approx (60.0));

        // Undo the sounds: the window follows by itself.
        f.project->undo();
        dispatch();
        panel.refresh();
        CHECK (panel.getSoundText() == "Pluck");
        f.project->undo();
        dispatch();
        panel.refresh();
        CHECK (panel.getSoundText() == "Choose a sound...");
        CHECK (panel.getInfoText().isEmpty());

        // The triangle plays the root key while it is held.
        panel.setAuditioning (true);
        CHECK (panel.isAuditioning());
        panel.setAuditioning (false);

        // The file gone: the window says so.
        f.project->redo();
        f.project->redo();
        dispatch();
        f.pad.deleteFile();
        panel.refresh();
        CHECK (panel.getSoundText() == "Missing: Pad 7.wav");
    }

    TEST_CASE ("the sampler opens that window; it is in the list of instruments and in the menu of new tracks")
    {
        SamplerFixture f;
        ui::PluginWindow window (*f.instrument);
        CHECK (window.showsKnobs());
        CHECK (dynamic_cast<ui::SamplerPanel*> (window.getKnobs()) != nullptr);

        // The list of instruments: shows it, and gives it to another track.
        ui::InstrumentPanel own (test::session(), *f.samplerTrack);
        CHECK (own.getInstrumentText() == "Sampler (built-in)");

        ui::InstrumentPanel other (test::session(), *f.synthTrack);
        std::optional<audio::plugin_catalog::Entry> samplerEntry;

        for (const auto& entry : audio::plugin_catalog::getAll (test::session(), audio::plugin_catalog::Kind::instrument))
            if (entry.name == "Sampler")
                samplerEntry = entry;

        REQUIRE (samplerEntry.has_value());
        other.choose (*samplerEntry);
        CHECK (instruments::playsSampler (*f.synthTrack));
        CHECK (other.getInstrumentText() == "Sampler (built-in)");
        CHECK (f.project->getUndoDescription() == "Change instrument");
        f.project->undo();
        dispatch();
        CHECK_FALSE (instruments::playsSampler (*f.synthTrack));

        // A new sampler track: with a sound (named after it), or empty; one undo step.
        ui::arrangement::ArrangementModel model (*f.project);
        const auto tracksBefore = f.project->getNumTracks();
        juce::String error;
        auto* track = model.addSamplerTrack (f.pluck, error);
        REQUIRE (track != nullptr);
        CHECK (error.isEmpty());
        CHECK (track->getName() == "Pluck");
        CHECK (instruments::playsSampler (*track));
        CHECK (sampler::getFile (*instruments::get (*track)) == f.pluck);
        CHECK (dynamic_cast<sampler::Plugin*> (instruments::get (*track))->getSample() != nullptr);
        CHECK (model.getCurrentTrack() == track);
        CHECK (model.getOpenClip() != nullptr);
        CHECK (f.project->getUndoDescription() == "Add sampler track");
        f.project->undo();
        dispatch();
        CHECK (f.project->getNumTracks() == tracksBefore);

        // A file that isn't a sound: said, and no track.
        CHECK (model.addSamplerTrack (f.text, error) == nullptr);
        CHECK (error.contains ("Notes.txt"));
        CHECK (f.project->getNumTracks() == tracksBefore);

        const auto menu = ui::arrangement::makeAddTrackMenu (model);
        REQUIRE (find (menu, "Sampler track").has_value());
        find (menu, "Sampler track")->action();
        REQUIRE (f.project->getNumTracks() == tracksBefore + 1);
        auto* empty = f.project->getTracks().getLast();
        CHECK (empty->getName() == "Sampler");
        CHECK (instruments::playsSampler (*empty));
        CHECK (sampler::getFile (*instruments::get (*empty)) == juce::File());
    }

    TEST_CASE ("piano roll: a sound dropped on a sampler track's keys becomes its sound; a key's menu makes it the root key")
    {
        SamplerFixture f;
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& model = main->getPianoRollModel();
        auto& keys = main->getPianoRoll().getKeys();
        auto& view = main->getPianoRoll().getView();
        main->getArrangementModel().openClip (f.samplerClip);
        dispatch();

        REQUIRE (model.getSampler() == f.instrument);
        CHECK (model.getSamplerRoot() == 60);
        CHECK (keys.isInterestedInFileDrag ({ f.pluck.getFullPathName() }));

        // Wherever over the keys it is: the root key lights up, and the sampler gets the sound.
        const auto y = juce::roundToInt (view.pitchToY (67) + view.rowHeight * 0.5f);
        keys.fileDragMove ({ f.pluck.getFullPathName() }, 10, y);
        CHECK (keys.getDropPitch() == 60);
        keys.filesDropped ({ f.pluck.getFullPathName() }, 10, y);
        CHECK (keys.getDropPitch() == -1);
        CHECK (sampler::getFile (*f.instrument) == f.pluck);
        CHECK (sampler::getRoot (*f.instrument) == 60);
        CHECK (f.project->getUndoDescription() == "Sampler: Pluck");

        // A key's menu: that key plays the sound as it is.
        auto menu = keys.createMenu (57);
        REQUIRE (find (menu, "This key (A3) plays the sound as it is").has_value());
        CHECK_FALSE (find (menu, "This key (A3)")->isTicked);
        CHECK (find (menu, "Choose a sound file for the sampler...").has_value());
        CHECK_FALSE (find (menu, "Choose a sound file for it...").has_value());
        find (menu, "This key (A3)")->action();
        CHECK (model.getSamplerRoot() == 57);
        CHECK (f.project->getUndoDescription() == "Root key");
        menu = keys.createMenu (57);
        CHECK (find (menu, "This key (A3)")->isTicked);
        f.project->undo();
        CHECK (model.getSamplerRoot() == 60);

        // What isn't a sound: said, nothing changed.
        CHECK (model.setSamplerSound (f.text).isNotEmpty());
        CHECK (sampler::getFile (*f.instrument) == f.pluck);

        // The keys paint with the root key's dot.
        const auto image = keys.createComponentSnapshot (keys.getLocalBounds());

        const auto dotY = juce::roundToInt (view.pitchToY (60) + view.rowHeight * 0.5f);
        REQUIRE (juce::isPositiveAndBelow (dotY, image.getHeight()));
        CHECK (image.getPixelAt (8, dotY) == ui::theme::colour::accent);

        // A track with another instrument: nothing about the sampler.
        main->getArrangementModel().openClip (f.synthClip);
        dispatch();
        CHECK (model.getSampler() == nullptr);
        CHECK (model.getSamplerRoot() == -1);
        CHECK_FALSE (keys.isInterestedInFileDrag ({ f.pluck.getFullPathName() }));
        CHECK (model.setSamplerSound (f.pluck).isNotEmpty());
        CHECK_FALSE (find (keys.createMenu (57), "This key").has_value());

        main.reset();
    }

    TEST_CASE ("sound browser: a sound's menu makes a sampler track of it, or gives it to the current sampler")
    {
        SamplerFixture f;
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& browser = main->getSoundBrowser();
        browser.setFolder (f.temp.folder);
        const auto row = browser.indexOf (f.pluck);
        REQUIRE (row >= 0);

        // The current track plays a synth: only the new track is offered.
        main->getArrangementModel().setCurrentTrack (f.synthTrack);
        dispatch();
        auto menu = browser.createMenu (row);
        REQUIRE (find (menu, "New sampler track playing this sound").has_value());
        CHECK_FALSE (find (menu, "Play this sound in the sampler").has_value());

        const auto tracksBefore = f.project->getNumTracks();
        find (menu, "New sampler track playing this sound")->action();
        dispatch();
        REQUIRE (f.project->getNumTracks() == tracksBefore + 1);
        auto* track = main->getArrangementModel().getCurrentTrack();
        REQUIRE (track != nullptr);
        CHECK (track->getName() == "Pluck");
        CHECK (instruments::playsSampler (*track));
        CHECK (sampler::getFile (*instruments::get (*track)) == f.pluck);

        // The piano roll is on the new track's clip: its keys play the sound.
        CHECK (main->getPianoRollModel().getSampler() == instruments::get (*track));

        // One step to take back, and it is this one.
        CHECK (f.project->getUndoDescription() == "Add sampler track");
        f.project->undo();
        dispatch();
        CHECK (f.project->getNumTracks() == tracksBefore);

        // The current track plays the sampler: the sound can go into it.
        main->getArrangementModel().setCurrentTrack (f.samplerTrack);
        dispatch();
        menu = browser.createMenu (row);
        REQUIRE (find (menu, "Play this sound in the sampler of Sampler").has_value());
        find (menu, "Play this sound in the sampler of Sampler")->action();
        CHECK (sampler::getFile (*f.instrument) == f.pluck);
        f.project->undo();
        CHECK (sampler::getFile (*f.instrument) == juce::File());

        main.reset();
    }
}
