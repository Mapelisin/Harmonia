#include <doctest.h>

#include "core/Markers.h"
#include "core/NoteEditor.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace markers = core::markers;

namespace
{
    juce::StringArray namesOf (const std::vector<markers::Marker>& all)
    {
        juce::StringArray names;

        for (const auto& m : all)
            names.add (m.name + "@" + juce::String (m.beat));

        return names;
    }

    bool runItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            const auto& item = it.getItem();

            if (item.text == text && item.isEnabled && item.action != nullptr)
            {
                item.action();
                return true;
            }
        }

        return false;
    }

    double playheadBeat (core::Project& project)
    {
        auto& edit = project.getEdit();
        return edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();
    }
}

TEST_SUITE ("markers")
{
    TEST_CASE ("markers are kept in time order, changed with undo and saved with the project")
    {
        auto& session = test::session();
        juce::TemporaryFile file (".harmonia");

        {
            auto project = core::Project::createEmpty (session);
            auto& edit = project->getEdit();
            CHECK (markers::getAll (edit).empty());

            project->beginGesture ("a");
            markers::add (edit, 16.0, "Chorus");
            project->beginGesture ("b");
            const auto verse = markers::add (edit, 0.0, "Verse");
            project->beginGesture ("c");
            markers::add (edit, -4.0, "Intro");      // not before the song's start
            CHECK (namesOf (markers::getAll (edit)) == juce::StringArray { "Verse@0", "Intro@0", "Chorus@16" });

            project->beginGesture ("d");
            markers::move (edit, markers::getAll (edit)[1], 32.0);
            markers::rename (edit, verse, "Verse 1");
            markers::rename (edit, verse, "   ");     // empty: ignored
            CHECK (namesOf (markers::getAll (edit)) == juce::StringArray { "Verse 1@0", "Chorus@16", "Intro@32" });

            project->undo();
            CHECK (namesOf (markers::getAll (edit)) == juce::StringArray { "Verse@0", "Intro@0", "Chorus@16" });
            project->redo();

            CHECK (markers::findAtOrBefore (edit, 20.0)->name == "Chorus");
            CHECK (markers::findAtOrBefore (edit, 16.0)->name == "Chorus");
            CHECK (markers::findAfter (edit, 16.0)->name == "Intro");
            CHECK_FALSE (markers::findAfter (edit, 32.0).has_value());

            const auto chorus = *markers::findAtOrBefore (edit, 16.0);
            CHECK (markers::getSection (edit, chorus, 64.0) == std::pair { 16.0, 32.0 });
            CHECK (markers::getSection (edit, *markers::findAtOrBefore (edit, 32.0), 64.0) == std::pair { 32.0, 64.0 });

            CHECK (project->hasUnsavedChanges());
            REQUIRE (project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = core::Project::load (session, file.getFile(), error);
        REQUIRE (loaded != nullptr);
        CHECK (namesOf (markers::getAll (loaded->getEdit())) == juce::StringArray { "Verse 1@0", "Chorus@16", "Intro@32" });

        markers::remove (loaded->getEdit(), markers::getAll (loaded->getEdit())[0]);
        CHECK (markers::getAll (loaded->getEdit()).size() == 2);
    }

    TEST_CASE ("new markers get the next name; a section can be looped; one undo step each")
    {
        auto project = core::Project::createEmpty (test::session());
        project->addMidiClip (*project->getTracks().getFirst(), 0.0, 64.0, "Song");
        arrangement::ArrangementModel model (*project);
        project->clearUndoHistory();

        CHECK (model.getNewMarkerName() == "Intro");
        model.addMarker (0.0, model.getNewMarkerName());
        CHECK (model.getNewMarkerName() == "Verse");
        const auto verse = model.addMarker (16.0, "");
        CHECK (verse.name == "Verse");
        model.addMarker (32.0, "Chorus");
        CHECK (model.getNewMarkerName() == "Pre-chorus");

        model.loopSection (verse);
        CHECK (model.getPlayMode() == arrangement::PlayMode::song);
        REQUIRE (model.getLoopRegion().has_value());
        CHECK (model.getLoopRegion()->start == doctest::Approx (16.0));
        CHECK (model.getLoopRegion()->end == doctest::Approx (32.0));

        // The last section goes to the song's end.
        model.loopSection (model.getMarkers().back());
        CHECK (model.getLoopRegion()->end == doctest::Approx (64.0));

        model.renameMarker (verse, "Verse 1");
        model.removeMarker (model.getMarkers()[0]);
        CHECK (namesOf (model.getMarkers()) == juce::StringArray { "Verse 1@16", "Chorus@32" });

        project->undo();
        project->undo();
        CHECK (namesOf (model.getMarkers()) == juce::StringArray { "Intro@0", "Verse@16", "Chorus@32" });
        project->undo();
        CHECK (model.getMarkers().size() == 2);
    }

    TEST_CASE ("the ruler: flags, its menu, the + Marker button; Ctrl+Left / Ctrl+Right play from the markers")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& model = main.getArrangementModel();
        auto& arrangement = main.getArrangement();
        auto& ruler = arrangement.getRuler();

        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 64.0, "Song");
        core::NoteEditor (*clip).add (core::Note {});
        model.addMarker (0.0, "Intro");
        model.addMarker (16.0, "Verse");
        model.addMarker (48.0, "Chorus");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        // The flags are in the strip at the top, where the markers are.
        const auto& view = arrangement.getView();
        const auto verseX = view.beatToX (16.0);
        REQUIRE (ruler.findMarkerAt ({ verseX + 6.0f, 8.0f }).has_value());
        CHECK (ruler.findMarkerAt ({ verseX + 6.0f, 8.0f })->name == "Verse");
        CHECK_FALSE (ruler.findMarkerAt ({ verseX + 6.0f, arrangement::ArrangementRuler::markerLaneHeight + 4.0f }).has_value());
        CHECK_FALSE (ruler.findMarkerAt ({ view.beatToX (36.0), 8.0f }).has_value());

        // The menu: add with a name; loop, rename, delete a marker.
        CHECK (runItem (ruler.createMenu (32.0, std::nullopt), "Bridge"));
        CHECK (namesOf (model.getMarkers()) == juce::StringArray { "Intro@0", "Verse@16", "Bridge@32", "Chorus@48" });

        std::optional<juce::String> renaming;
        ruler.onRenameMarker = [&renaming] (const markers::Marker& m) { renaming = m.name; };
        const auto verse = model.getMarkers()[1];
        CHECK (runItem (ruler.createMenu (16.0, verse), "Rename..."));
        CHECK (renaming == juce::String ("Verse"));
        CHECK (runItem (ruler.createMenu (16.0, verse), "Loop this section"));
        CHECK (model.getLoopRegion()->start == doctest::Approx (16.0));
        CHECK (model.getLoopRegion()->end == doctest::Approx (32.0));
        CHECK (runItem (ruler.createMenu (32.0, model.getMarkers()[2]), "Delete marker"));
        CHECK (model.getMarkers().size() == 3);

        // Ctrl+Right / Ctrl+Left go from marker to marker.
        const juce::ModifierKeys ctrl (juce::ModifierKeys::ctrlModifier);
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, ctrl, 0)));
        CHECK (playheadBeat (*project) == doctest::Approx (16.0).epsilon (0.001));
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, ctrl, 0)));
        CHECK (playheadBeat (*project) == doctest::Approx (48.0).epsilon (0.001));
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::leftKey, ctrl, 0)));
        CHECK (playheadBeat (*project) == doctest::Approx (16.0).epsilon (0.001));

        // "+ Marker" puts the next name where the playhead is.
        arrangement.addMarkerAtPlayhead();
        CHECK (model.getMarkers().size() == 4);
        CHECK (namesOf (model.getMarkers()).contains ("Verse@16"));
        CHECK (namesOf (model.getMarkers()).contains ("Pre-chorus@16"));

        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-markers.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
