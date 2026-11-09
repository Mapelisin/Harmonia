#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/Clips.h"
#include "core/TestProject.h"
#include "ui/FileDrag.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
using browser::Entry;
using browser::SoundBrowser;
namespace clips = core::clips;

namespace
{
    void writeMidi (const juce::File& file)
    {
        juce::MidiMessageSequence notes;
        notes.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0.0);
        notes.addEvent (juce::MidiMessage::noteOff (1, 40), 960.0);

        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (480);
        midi.addTrack (notes);

        file.deleteFile();
        juce::FileOutputStream out (file);
        midi.writeTo (out);
    }

    /** A folder of sounds: Drums/hat.wav, kick.wav, Snare 2.wav, snare 10.wav, bass.mid and a text file. */
    struct SoundFolder
    {
        SoundFolder()
        {
            temp.getFile ("Drums").createDirectory();
            test::writeTone (temp.getFile ("Drums").getChildFile ("hat.wav"), 0.1);
            kick = test::writeTone (temp.getFile ("kick.wav"), 0.5, 0.6f);
            test::writeTone (temp.getFile ("snare 10.wav"), 0.2);
            snare = test::writeTone (temp.getFile ("Snare 2.wav"), 0.2);
            writeMidi (temp.getFile ("bass.mid"));
            temp.getFile ("readme.txt").replaceWithText ("not a sound");
        }

        test::TempFolder temp;
        juce::File kick, snare;
    };

    juce::StringArray namesOf (const std::vector<Entry>& entries)
    {
        juce::StringArray names;

        for (const auto& e : entries)
            names.add (e.file.getFileName());

        return names;
    }
}

TEST_SUITE ("sound browser")
{
    TEST_CASE ("a folder shows its folders, then its sounds and MIDI files by name; the search keeps what matches")
    {
        SoundFolder f;
        audio::FilePreview preview (test::session());
        const auto isSound = [&preview] (const juce::File& file) { return preview.canPlay (file); };

        const auto all = browser::listFolder (f.temp.folder, {}, isSound);
        CHECK (namesOf (all) == juce::StringArray { "Drums", "bass.mid", "kick.wav", "Snare 2.wav", "snare 10.wav" });
        CHECK (all[0].kind == Entry::Kind::folder);
        CHECK (all[1].kind == Entry::Kind::midi);
        CHECK (all[2].kind == Entry::Kind::sound);

        CHECK (namesOf (browser::listFolder (f.temp.folder, " SNARE", isSound)) == juce::StringArray { "Snare 2.wav", "snare 10.wav" });
        CHECK (browser::listFolder (f.temp.folder, "no such sound", isSound).empty());
        CHECK (browser::listFolder (f.temp.getFile ("missing"), {}, isSound).empty());
        CHECK_FALSE (preview.canPlay (f.temp.getFile ("readme.txt")));
    }

    TEST_CASE ("a click plays a sound or opens a folder; Up / Down play the next; Backspace goes up; Esc stops")
    {
        SoundFolder f;
        audio::FilePreview preview (test::session());
        SoundBrowser browser (test::session(), preview);
        browser.setBounds (0, 0, 1000, 300);
        browser.setFolder (f.temp.folder);
        REQUIRE (browser.getEntries().size() == 5);

        int changes = 0;
        const auto onChange = preview.onStateChanged;
        preview.onStateChanged = [&] { ++changes; onChange(); };

        // A sound: plays; again: stops.
        browser.clickEntry (browser.indexOf (f.kick));
        CHECK (preview.isPlaying());
        CHECK (preview.getFile() == f.kick);
        CHECK (preview.getLengthSeconds() == doctest::Approx (0.5).epsilon (0.01));
        browser.clickEntry (browser.indexOf (f.kick));
        CHECK_FALSE (preview.isPlaying());
        CHECK (preview.getFile() == juce::File());
        CHECK (changes == 2);

        // Down: the next row, and it plays.
        auto& list = browser.getList();
        list.selectRow (browser.indexOf (f.kick));
        CHECK (list.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
        CHECK (list.getSelectedRow() == browser.indexOf (f.snare));
        CHECK (preview.getFile() == f.snare);

        // Esc stops it.
        CHECK (browser.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK_FALSE (preview.isPlaying());
        CHECK_FALSE (browser.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));

        // A MIDI file plays with the built-in sound: its one note for a second, then a second to ring out.
        browser.clickEntry (browser.indexOf (f.temp.getFile ("bass.mid")));
        CHECK (preview.isPlaying());
        CHECK (preview.getFile() == f.temp.getFile ("bass.mid"));
        CHECK (preview.getLengthSeconds() == doctest::Approx (2.0).epsilon (0.01));
        browser.clickEntry (browser.indexOf (f.temp.getFile ("bass.mid")));
        CHECK_FALSE (preview.isPlaying());

        // A folder opens; Backspace goes back up, with the folder selected.
        browser.clickEntry (browser.indexOf (f.temp.getFile ("Drums")));
        CHECK (browser.getFolder() == f.temp.getFile ("Drums"));
        CHECK (namesOf (browser.getEntries()) == juce::StringArray { "hat.wav" });
        CHECK (browser.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)));
        CHECK (browser.getFolder() == f.temp.folder);
        CHECK (list.getSelectedRow() == browser.indexOf (f.temp.getFile ("Drums")));

        // The search; a new folder clears it.
        browser.setFilter ("kick");
        CHECK (namesOf (browser.getEntries()) == juce::StringArray { "kick.wav" });
        browser.setFolder (f.temp.getFile ("Drums"));
        CHECK (browser.getFilter().isEmpty());
        CHECK (browser.getEntries().size() == 1);

        // A double-click (or Enter) hands a sound to the song.
        browser.setFolder (f.temp.folder);
        juce::File added;
        browser.onAddToSong = [&added] (const juce::File& file) { added = file; };
        browser.clickEntry (browser.indexOf (f.kick));
        browser.openEntry (browser.indexOf (f.kick));
        CHECK (added == f.kick);
        CHECK_FALSE (preview.isPlaying());

        // Painting with a sound playing.
        browser.clickEntry (browser.indexOf (f.snare));
        CHECK (browser.createComponentSnapshot (browser.getLocalBounds()).isValid());
        preview.stop();
    }

    TEST_CASE ("the search looks in the subfolders too: what's right in the folder first, then by name")
    {
        SoundFolder f;
        audio::FilePreview preview (test::session());
        const auto isSound = [&preview] (const juce::File& file) { return preview.canPlay (file); };
        const auto hat = f.temp.getFile ("Drums").getChildFile ("hat.wav");

        const auto found = browser::listFolder (f.temp.folder, "HAT", isSound);
        REQUIRE (found.size() == 1);
        CHECK (found[0].file == hat);
        CHECK (found[0].kind == Entry::Kind::sound);

        // Folders are found only right in the folder; files anywhere below.
        f.temp.getFile ("Drums").getChildFile ("Wav kit").createDirectory();
        CHECK (namesOf (browser::listFolder (f.temp.folder, "wav", isSound))
               == juce::StringArray { "kick.wav", "Snare 2.wav", "snare 10.wav", "hat.wav" });

        // Without a search, only the folder itself.
        const auto plain = browser::listFolder (f.temp.folder, {}, isSound);
        CHECK (std::none_of (plain.begin(), plain.end(), [&hat] (const Entry& e) { return e.file == hat; }));

        SoundBrowser browser (test::session(), preview);
        browser.setBounds (0, 0, 1000, 300);
        browser.setFolder (f.temp.folder);
        browser.setFilter ("hat");
        REQUIRE (browser.getEntries().size() == 1);
        browser.clickEntry (0);
        CHECK (preview.getFile() == hat);
        CHECK (browser.createComponentSnapshot (browser.getLocalBounds()).isValid());
        preview.stop();
    }

    TEST_CASE ("places: the usual folders, and the ones added are kept (as is the last folder)")
    {
        SoundFolder f;
        audio::FilePreview preview (test::session());

        const auto hasPlace = [] (const SoundBrowser& b, const juce::File& folder)
        {
            for (const auto& p : b.getPlaces())
                if (p.folder == folder)
                    return p.added;

            return false;
        };

        {
            SoundBrowser browser (test::session(), preview);
            CHECK_FALSE (browser.getPlaces().empty());
            CHECK_FALSE (hasPlace (browser, f.temp.folder));

            browser.addPlace (f.temp.folder);
            CHECK (hasPlace (browser, f.temp.folder));
            CHECK (browser.getFolder() == f.temp.folder);
            browser.addPlace (f.temp.folder);     // not twice
            CHECK (std::count_if (browser.getPlaces().begin(), browser.getPlaces().end(),
                                  [&f] (const browser::Place& p) { return p.folder == f.temp.folder; }) == 1);
            browser.setFolder (f.temp.getFile ("Drums"));
        }

        {
            SoundBrowser browser (test::session(), preview);
            CHECK (hasPlace (browser, f.temp.folder));
            CHECK (browser.getFolder() == f.temp.getFile ("Drums"));

            browser.removePlace (f.temp.folder);
            CHECK_FALSE (hasPlace (browser, f.temp.folder));
        }

        SoundBrowser browser (test::session(), preview);
        CHECK_FALSE (hasPlace (browser, f.temp.folder));
    }

    TEST_CASE ("the main window: F6 shows the sounds; double-click adds at the playhead; a drag drops onto a track")
    {
        SoundFolder f;
        auto project = core::Project::createEmpty (test::session());
        auto main = std::make_unique<MainComponent> (test::session(), *project);
        main->setBounds (0, 0, 1280, 800);
        auto& browser = main->getSoundBrowser();
        auto& model = main->getArrangementModel();
        auto& arrangement = main->getArrangement();

        CHECK (main->keyPressed (juce::KeyPress (juce::KeyPress::F6Key)));
        CHECK (main->getLowerPanel() == MainComponent::LowerPanel::sounds);
        CHECK (browser.isVisible());
        CHECK_FALSE (main->getPianoRoll().isVisible());

        browser.setFolder (f.temp.folder);
        main->getTransport().setPositionSeconds (2.0);     // beat 4 at 120 BPM

        // Double-click: the instrument track can't hold a sound, so it goes onto a new audio track.
        browser.openEntry (browser.indexOf (f.kick));
        REQUIRE (model.getNumTracks() == 2);
        auto onTrack = clips::getClips (*model.getTrack (1));
        REQUIRE (onTrack.size() == 1);
        CHECK (clips::getStartBeat (*onTrack[0]) == doctest::Approx (4.0));

        // A drag from the list onto that track, at bar 3.
        auto& view = arrangement.getView();
        view.pixelsPerBeat = 16.0;
        view.leftBeat = 0.0;
        model.setSnap (core::snap::Mode::bar);
        const auto at = arrangement.getLocalPoint (&arrangement.getClipArea(),
                                                   juce::Point<float> (view.beatToX (9.0), view.trackToY (1) + 20.0f)).toInt();

        const juce::DragAndDropTarget::SourceDetails notOurs (juce::var ("something else"), &browser.getList(), at);
        CHECK_FALSE (arrangement.isInterestedInDragSource (notOurs));

        const juce::DragAndDropTarget::SourceDetails drag (file_drag::describe ({ f.snare.getFullPathName() }), &browser.getList(), at);
        REQUIRE (arrangement.isInterestedInDragSource (drag));
        arrangement.itemDragEnter (drag);
        CHECK (arrangement.getClipArea().getDropMarker().has_value());
        arrangement.itemDropped (drag);
        CHECK_FALSE (arrangement.getClipArea().getDropMarker().has_value());

        onTrack = clips::getClips (*model.getTrack (1));
        REQUIRE (onTrack.size() == 2);
        CHECK (clips::getStartBeat (*onTrack[1]) == doctest::Approx (8.0));

        // One undo step each.
        project->undo();
        CHECK (clips::getClips (*model.getTrack (1)).size() == 1);

        // Listening stops when another panel shows.
        browser.clickEntry (browser.indexOf (f.kick));
        CHECK (main->getFilePreview().isPlaying());
        browser.getList().selectRow (browser.indexOf (f.kick));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);

        const auto image = main->createComponentSnapshot (main->getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-sounds.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        // F7: the editor of the current track, the audio track the sounds went onto.
        CHECK (main->keyPressed (juce::KeyPress (juce::KeyPress::F7Key)));
        CHECK_FALSE (main->getFilePreview().isPlaying());
        CHECK (main->editsAudio());
        CHECK (main->getAudioEditor().isVisible());
        CHECK_FALSE (main->getPianoRoll().isVisible());

        main.reset();
    }

    TEST_CASE ("a drag of files carries their paths")
    {
        const juce::StringArray paths { "C:\\Sounds\\kick.wav", "C:\\Sounds\\snare.wav" };
        CHECK (file_drag::read (file_drag::describe (paths)) == paths);
        CHECK (file_drag::read (juce::var ("C:\\Sounds\\kick.wav")).isEmpty());
        CHECK (file_drag::read (juce::var (3)).isEmpty());
    }
}
