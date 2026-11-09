#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "ui/CpuMeter.h"
#include "ui/MainComponent.h"
#include "ui/PositionDisplay.h"

using namespace harmonia;

namespace
{
    struct MainFixture
    {
        MainFixture()
        {
            auto& track = *project->getTracks().getFirst();
            track.setName ("Keys");
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

        /** The item whose text starts with `text`, in `menu` or its submenus. */
        static std::optional<juce::PopupMenu::Item> find (const juce::PopupMenu& menu, const juce::String& text)
        {
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (it.getItem().text.startsWith (text))
                    return it.getItem();

            return std::nullopt;
        }

        static juce::StringArray texts (const juce::PopupMenu& menu)
        {
            juce::StringArray result;

            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text.isNotEmpty())
                    result.add (it.getItem().text);

            return result;
        }

        /** Chooses the item (it must be there and enabled). */
        static void choose (const juce::PopupMenu& menu, const juce::String& text)
        {
            const auto item = find (menu, text);
            REQUIRE_MESSAGE (item.has_value(), text.toStdString());
            REQUIRE_MESSAGE (item->isEnabled, text.toStdString());
            REQUIRE (item->action != nullptr);
            item->action();
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main;
    };
}

TEST_SUITE ("ui: the menu bar and the transport")
{
    TEST_CASE ("the menu bar has File, Edit, View, Track, Transport and Help, and every shortcut is written in its items")
    {
        MainFixture f;
        auto& menu = f.main->getMainMenu();
        CHECK (menu.getMenuBarNames() == juce::StringArray { "File", "Edit", "View", "Track", "Transport", "Help" });
        CHECK (f.main->getMenuBar().isVisible());

        for (int i = 0; i < 6; ++i)
            CHECK (menu.getMenuForIndex (i, {}).containsAnyActiveItems());

        // Shortcuts are shown next to the commands.
        const auto edit = menu.createEditMenu();
        CHECK (MainFixture::find (edit, "Undo")->shortcutKeyDescription == "Ctrl+Z");
        CHECK (MainFixture::find (edit, "Copy")->shortcutKeyDescription == "Ctrl+C");
        CHECK (MainFixture::find (menu.createViewMenu(), "Mixer")->shortcutKeyDescription == "F9");
        CHECK (MainFixture::find (menu.createTransportMenu(), "Back to the start")->shortcutKeyDescription == "Home");

        // File keeps its commands and gains the settings and Exit.
        const auto file = f.main->createFileMenu();
        CHECK (MainFixture::find (file, "Save").has_value());
        CHECK (MainFixture::find (file, "Settings")->shortcutKeyDescription == "Ctrl+,");
        CHECK (MainFixture::find (file, "Export song")->text.contains ("MP3"));
        CHECK (MainFixture::find (file, "Exit")->shortcutKeyDescription == "Alt+F4");
    }

    TEST_CASE ("Edit: undo and redo say what they take back, and do it")
    {
        MainFixture f;
        auto& menu = f.main->getMainMenu();

        auto undo = MainFixture::find (menu.createEditMenu(), "Undo");
        CHECK (undo->text == "Undo");
        CHECK_FALSE (undo->isEnabled);

        f.project->beginGesture ("Change tempo");
        f.project->setTempoBpm (133.0);

        undo = MainFixture::find (menu.createEditMenu(), "Undo");
        CHECK (undo->text == "Undo \"Change tempo\"");
        MainFixture::choose (menu.createEditMenu(), "Undo");
        CHECK (f.project->getTempoBpm() == doctest::Approx (120.0));

        CHECK (MainFixture::find (menu.createEditMenu(), "Redo")->text == "Redo \"Change tempo\"");
        MainFixture::choose (menu.createEditMenu(), "Redo");
        CHECK (f.project->getTempoBpm() == doctest::Approx (133.0));
    }

    TEST_CASE ("Edit works on the notes while the piano roll is shown, else on the clips")
    {
        MainFixture f;
        auto& menu = f.main->getMainMenu();
        auto& notes = f.main->getPianoRollModel();
        REQUIRE (notes.getClip() != nullptr);

        // The piano roll is shown: notes.
        notes.beginGesture ("test");

        for (const auto& [pitch, start] : { std::pair { 60, 0.0 }, std::pair { 64, 1.0 } })
        {
            core::Note note;
            note.pitch = pitch;
            note.startBeat = start;
            note.lengthBeats = 1.0;
            REQUIRE (notes.addNote (note) != nullptr);
        }
        CHECK_FALSE (f.main->isArrangementTheEditTarget());
        CHECK (MainFixture::texts (menu.createEditMenu()).contains ("Notes in the piano roll"));

        MainFixture::choose (menu.createEditMenu(), "Select all");
        CHECK (notes.getSelectedNotes().size() == 2);
        MainFixture::choose (menu.createEditMenu(), "Delete");
        CHECK (notes.getClip()->getSequence().getNumNotes() == 0);

        // The mixer is shown: the clips.
        f.main->showLowerPanel (ui::MainComponent::LowerPanel::mixer);
        CHECK (f.main->isArrangementTheEditTarget());
        const auto edit = menu.createEditMenu();
        CHECK (MainFixture::texts (edit).contains ("Clips in the arrangement"));
        CHECK_FALSE (MainFixture::find (edit, "Copy")->isEnabled);       // nothing selected

        MainFixture::choose (menu.createEditMenu(), "Select all");
        CHECK (f.main->getArrangementModel().getSelectedClips().size() == 1);
        MainFixture::choose (menu.createEditMenu(), "Duplicate as independent copies");
        CHECK (core::clips::getClips (*f.project->getTracks().getFirst()).size() == 2);
    }

    TEST_CASE ("View switches the panel below and ticks the one shown")
    {
        MainFixture f;
        auto& menu = f.main->getMainMenu();
        using Panel = ui::MainComponent::LowerPanel;

        CHECK (MainFixture::find (menu.createViewMenu(), "Piano roll")->isTicked);
        MainFixture::choose (menu.createViewMenu(), "Mixer");
        CHECK (f.main->getLowerPanel() == Panel::mixer);
        CHECK (f.main->getMixer().isVisible());
        CHECK (MainFixture::find (menu.createViewMenu(), "Mixer")->isTicked);
        CHECK_FALSE (MainFixture::find (menu.createViewMenu(), "Piano roll")->isTicked);

        MainFixture::choose (menu.createViewMenu(), "Keyboard shortcuts");
        CHECK (f.main->areShortcutsShown());
    }

    TEST_CASE ("Track adds tracks and has the current track's own menu")
    {
        MainFixture f;
        auto& menu = f.main->getMainMenu();
        const auto track = menu.createTrackMenu();
        const auto items = MainFixture::texts (track);

        CHECK (items.contains ("\"Keys\""));
        CHECK (MainFixture::find (track, "Drum track").has_value());
        CHECK (MainFixture::find (track, "Delete track").has_value());
        CHECK_FALSE (MainFixture::find (track, "Add track").has_value());     // "Add" is at the top instead

        MainFixture::choose (menu.createTrackMenu(), "Duplicate track");
        CHECK (f.project->getNumTracks() == 2);
        MainFixture::choose (menu.createTrackMenu(), "Audio track");
        CHECK (f.project->getNumTracks() == 3);
    }

    TEST_CASE ("Transport: metronome, play mode, count-in and back to the start")
    {
        MainFixture f;
        auto& menu = f.main->getMainMenu();
        auto& edit = f.project->getEdit();
        auto& transport = f.main->getTransport();

        const auto clickWasOn = edit.clickTrackEnabled.get();
        MainFixture::choose (menu.createTransportMenu(), "Metronome");
        CHECK (edit.clickTrackEnabled.get() != clickWasOn);
        CHECK (f.main->getTransportBar().getClickButton().getToggleState() == edit.clickTrackEnabled.get());

        MainFixture::choose (menu.createTransportMenu(), "Play the song");
        CHECK (f.main->getArrangementModel().getPlayMode() == ui::arrangement::PlayMode::song);
        MainFixture::choose (menu.createTransportMenu(), "Play the open clip");
        CHECK (f.main->getArrangementModel().getPlayMode() == ui::arrangement::PlayMode::clip);

        MainFixture::choose (menu.createTransportMenu(), "2 bars");
        CHECK (transport.getCountIn() == audio::Transport::CountIn::twoBars);
        transport.setCountIn (audio::Transport::CountIn::off);

        // Home, the button, the menu, and Stop while stopped all go back to the start.
        transport.setPositionSeconds (3.0);
        CHECK (f.main->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
        CHECK (transport.getPositionSeconds() == doctest::Approx (0.0));

        transport.setPositionSeconds (3.0);
        MainFixture::choose (menu.createTransportMenu(), "Back to the start");
        CHECK (transport.getPositionSeconds() == doctest::Approx (0.0));

        transport.setPositionSeconds (3.0);
        f.main->getTransportBar().getStopButton().triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        CHECK (transport.getPositionSeconds() == doctest::Approx (0.0));
    }

    TEST_CASE ("the counter shows bar.beat.sixteenth and the time")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& tempo = project->getEdit().tempoSequence;
        project->setTempoBpm (120.0);       // a beat is half a second

        using ui::PositionDisplay;
        CHECK (PositionDisplay::barsBeatsText (tempo, 0.0) == "1.1.1");
        CHECK (PositionDisplay::barsBeatsText (tempo, 0.125) == "1.1.2");     // a sixteenth in
        CHECK (PositionDisplay::barsBeatsText (tempo, 0.5) == "1.2.1");
        CHECK (PositionDisplay::barsBeatsText (tempo, 1.875) == "1.4.4");
        CHECK (PositionDisplay::barsBeatsText (tempo, 2.0) == "2.1.1");
        CHECK (PositionDisplay::barsBeatsText (tempo, 2.0 - 1.0e-9) == "2.1.1"); // rounding doesn't show "1.4.4"

        project->setBeatsPerBar (3);
        CHECK (PositionDisplay::barsBeatsText (tempo, 1.5) == "2.1.1");

        CHECK (PositionDisplay::clockText (0.0) == "0:00.000");
        CHECK (PositionDisplay::clockText (8.25) == "0:08.250");
        CHECK (PositionDisplay::clockText (65.25) == "1:05.250");
        CHECK (PositionDisplay::clockText (-1.5) == "-0:01.500");

        // The transport shows it.
        MainFixture f;
        auto& bar = f.main->getTransportBar();
        f.main->getTransport().setPositionSeconds (2.5);
        bar.refresh();
        CHECK (bar.getPositionDisplay().getMainText() == "2.2.1");
        CHECK (bar.getPositionDisplay().getSecondaryText() == "0:02.500");
    }

    TEST_CASE ("the play and record buttons light up; the audio load meter reads 0..1 and warns when high")
    {
        MainFixture f;
        auto& bar = f.main->getTransportBar();
        CHECK_FALSE (bar.getPlayButton().getToggleState());
        CHECK_FALSE (bar.getRecordButton().getToggleState());

        f.main->getNoteInput().setRecordArmed (true);
        bar.refresh();
        CHECK (bar.getRecordButton().getToggleState());
        f.main->getNoteInput().setRecordArmed (false);

        std::optional<float> load;
        ui::CpuMeter meter ([&load] { return load; });
        CHECK_FALSE (meter.getLoad().has_value());

        load = 0.3f;
        meter.refresh();
        CHECK (*meter.getLoad() == doctest::Approx (0.3f));
        load = 1.7f;
        meter.refresh();
        CHECK (*meter.getLoad() == doctest::Approx (1.0f));

        CHECK (ui::CpuMeter::getColour (0.3f) != ui::CpuMeter::getColour (0.7f));
        CHECK (ui::CpuMeter::getColour (0.7f) != ui::CpuMeter::getColour (0.95f));
    }
}
