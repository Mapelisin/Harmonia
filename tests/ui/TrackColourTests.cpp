#include <doctest.h>

#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/TestProject.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/TrackColours.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace clips = core::clips;
namespace tracks = core::tracks;

namespace
{
    juce::Colour paletteColour (const juce::String& name)
    {
        for (const auto& c : track_colours::getPalette())
            if (c.name == name)
                return c.colour;

        FAIL ("no colour " << name);
        return {};
    }

    /** Runs the item with this text (in submenus too); false if there is none or it's disabled. */
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

    std::optional<juce::String> findTicked (const juce::PopupMenu& menu)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().isTicked)
                return it.getItem().text;

        return std::nullopt;
    }

    const juce::PopupMenu* findSubMenu (const juce::PopupMenu& menu, const juce::String& textStart)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().text.startsWith (textStart) && it.getItem().subMenu != nullptr)
                return it.getItem().subMenu.get();

        return nullptr;
    }
}

TEST_SUITE ("track and clip colours")
{
    TEST_CASE ("a track is coloured by its place unless it has a colour; one undo step; saved")
    {
        auto& session = test::session();
        juce::TemporaryFile file (".harmonia");
        const auto red = paletteColour ("Red");

        {
            auto project = core::Project::createEmpty (session);
            arrangement::ArrangementModel model (*project);
            auto& first = *project->getTracks().getFirst();
            auto& second = *project->addTrack ("Two");
            project->clearUndoHistory();

            CHECK_FALSE (tracks::getColour (first).has_value());
            CHECK (track_colours::getTrackColour (first, 0) == track_colours::getAutomatic (0));
            CHECK (track_colours::getAutomatic (0) != track_colours::getAutomatic (1));

            model.setTrackColour (second, red);
            CHECK (tracks::getColour (second) == red);
            CHECK (track_colours::getTrackColour (second, 1) == red);
            CHECK (project->hasUnsavedChanges());

            project->undo();
            CHECK_FALSE (tracks::getColour (second).has_value());
            project->redo();
            CHECK (tracks::getColour (second) == red);

            // Moved, it keeps its colour; the automatic one goes with the place.
            model.moveTrack (second, 0);
            CHECK (track_colours::getTrackColour (second, 0) == red);
            CHECK (track_colours::getTrackColour (first, 1) == track_colours::getAutomatic (1));

            REQUIRE (project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = core::Project::load (session, file.getFile(), error);
        REQUIRE (loaded != nullptr);
        CHECK (tracks::getColour (*loaded->getTracks().getFirst()) == red);
        CHECK_FALSE (tracks::getColour (*loaded->getTracks()[1]).has_value());

        // Back to automatic: nothing left in the file.
        arrangement::ArrangementModel model (*loaded);
        model.setTrackColour (*loaded->getTracks().getFirst(), std::nullopt);
        CHECK_FALSE (tracks::getColour (*loaded->getTracks().getFirst()).has_value());
        CHECK_FALSE (loaded->getTracks().getFirst()->state.hasProperty (te::IDs::colour));
    }

    TEST_CASE ("a clip takes its track's colour unless it has one; copies keep it; saved")
    {
        auto& session = test::session();
        juce::TemporaryFile file (".harmonia");
        const auto green = paletteColour ("Green"), blue = paletteColour ("Blue");

        {
            test::ProjectWithClip p;
            arrangement::ArrangementModel model (*p.project);
            model.setTrackColour (*p.track, blue);

            CHECK_FALSE (clips::getColour (*p.clip).has_value());
            CHECK (track_colours::getClipColour (*p.clip, 0) == blue);

            model.setClipColour ({ p.clip }, green);
            CHECK (track_colours::getClipColour (*p.clip, 0) == green);

            p.project->beginGesture ("Duplicate");
            auto* copy = core::clip_links::duplicate (*p.clip, 16.0, false);
            REQUIRE (copy != nullptr);
            CHECK (clips::getColour (*copy) == green);

            p.project->undo();     // the copy
            p.project->undo();     // the colour
            CHECK_FALSE (clips::getColour (*p.clip).has_value());
            p.project->redo();
            CHECK (clips::getColour (*p.clip) == green);

            REQUIRE (p.project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = core::Project::load (session, file.getFile(), error);
        REQUIRE (loaded != nullptr);
        const auto onTrack = clips::getClips (*loaded->getTracks().getFirst());
        REQUIRE (onTrack.size() == 1);
        CHECK (clips::getColour (*onTrack[0]) == green);
    }

    TEST_CASE ("the colour menu: automatic first, the colour in use ticked")
    {
        std::optional<juce::Colour> chosen;
        const auto choose = [&chosen] (std::optional<juce::Colour> c) { chosen = c; };

        auto menu = track_colours::createMenu (std::nullopt, "Automatic", choose);
        CHECK (findTicked (menu) == juce::String ("Automatic"));
        CHECK (menu.getNumItems() == 1 + (int) track_colours::getPalette().size());

        menu = track_colours::createMenu (paletteColour ("Purple"), "Automatic", choose);
        CHECK (findTicked (menu) == juce::String ("Purple"));

        CHECK (runItem (menu, "Teal"));
        CHECK (chosen == paletteColour ("Teal"));
        CHECK (runItem (menu, "Automatic"));
        CHECK (chosen == std::optional<juce::Colour>());
    }

    TEST_CASE ("the clip menu colours the selection in one undo step; the main window paints the colours")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& model = main.getArrangementModel();

        auto& first = *project->getTracks().getFirst();
        first.setName ("Drums");
        std::vector<te::MidiClip*> made;

        for (int i = 0; i < 4; ++i)
            made.push_back (project->addMidiClip (first, i * 8.0, 8.0, "Beat"));

        for (const auto& [name, colour] : { std::pair { "Bass", "Purple" }, { "Keys", "Amber" }, { "Lead", "Pink" } })
        {
            auto* track = model.addTrack();
            model.renameTrack (*track, name);
            model.setTrackColour (*track, paletteColour (colour));
            auto* clip = model.getOpenClip();
            core::NoteEditor editor (*clip);

            for (int i = 0; i < 8; ++i)
            {
                core::Note n;
                n.pitch = 48 + (i * 5) % 19;
                n.startBeat = i * 2.0;
                n.lengthBeats = 1.5;
                editor.add (n);
            }
        }

        project->clearUndoHistory();

        // Two of the four drum clips selected; the menu of one of them colours both.
        model.setSelection ({ made[1], made[2] });
        auto& arrangement = main.getArrangement();
        const auto menu = arrangement.createClipMenu (*made[1]);
        const auto* colourMenu = findSubMenu (menu, "Colour (2 clips)");
        REQUIRE (colourMenu != nullptr);
        CHECK (findTicked (*colourMenu) == juce::String ("Same as the track"));
        CHECK (runItem (*colourMenu, "Red"));

        CHECK (clips::getColour (*made[1]) == paletteColour ("Red"));
        CHECK (clips::getColour (*made[2]) == paletteColour ("Red"));
        CHECK_FALSE (clips::getColour (*made[0]).has_value());

        // An unselected clip's menu colours only it.
        const auto single = arrangement.createClipMenu (*made[3]);
        REQUIRE (findSubMenu (single, "Colour") != nullptr);
        CHECK (runItem (*findSubMenu (single, "Colour"), "Yellow"));
        CHECK (clips::getColour (*made[3]) == paletteColour ("Yellow"));
        CHECK (clips::getColour (*made[1]) == paletteColour ("Red"));

        project->undo();
        CHECK_FALSE (clips::getColour (*made[3]).has_value());
        project->undo();
        CHECK_FALSE (clips::getColour (*made[1]).has_value());
        CHECK_FALSE (clips::getColour (*made[2]).has_value());
        project->redo();
        project->redo();

        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-colours.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
