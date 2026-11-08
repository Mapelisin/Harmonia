#include <doctest.h>

#include "TestSession.h"
#include "core/Clips.h"
#include "core/Markers.h"
#include "core/Project.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;

namespace
{
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

    juce::String beats (double beat)    { return juce::String (std::round (beat * 1000.0) / 1000.0); }

    /** "name start-end" for each clip of the track, in time order. */
    juce::StringArray layout (te::AudioTrack& track)
    {
        juce::StringArray result;

        for (auto* clip : core::clips::getClips (track))
            result.add (clip->getName() + " " + beats (core::clips::getStartBeat (*clip)) + "-" + beats (core::clips::getEndBeat (*clip)));

        return result;
    }

    struct Fixture
    {
        Fixture()
        {
            main.setBounds (0, 0, 1280, 800);
            keys = project->getTracks().getFirst();
            project->addMidiClip (*keys, 0.0, 8.0, "A");
            project->addMidiClip (*keys, 8.0, 8.0, "B");
            project->clearUndoHistory();
        }

        arrangement::ArrangementModel& model()      { return main.getArrangementModel(); }
        arrangement::ArrangementRuler& ruler()      { return main.getArrangement().getRuler(); }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        MainComponent main { test::session(), *project };
        te::AudioTrack* keys = nullptr;
    };

    std::optional<std::pair<double, double>> loopOf (arrangement::ArrangementModel& model)
    {
        if (auto loop = model.getLoopRegion())
            return std::pair { loop->start, loop->end };

        return std::nullopt;
    }
}

TEST_SUITE ("arrangement bars")
{
    TEST_CASE ("the ruler's menu: the clicked bar on every track")
    {
        Fixture f;
        const juce::StringArray untouched { "A 0-8", "B 8-16" };

        // A click in bar 3 (beats 8-12): it snapped to beat 8, but the bar is where it was.
        auto menu = f.ruler().createMenu (8.0, std::nullopt, 9.5);
        CHECK (runItem (menu, "Duplicate bar 3 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "B 8-12", "B 12-16", "B 16-20" });
        REQUIRE (f.model().getSelectedClips().size() == 1);
        CHECK (core::clips::getStartBeat (*f.model().getSelectedClips()[0]) == doctest::Approx (12.0));
        f.model().undo();
        CHECK (layout (*f.keys) == untouched);

        menu = f.ruler().createMenu (4.0, std::nullopt, 5.0);
        CHECK (runItem (menu, "Insert an empty bar before bar 2 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-4", "A 8-12", "B 12-20" });
        f.model().undo();

        menu = f.ruler().createMenu (4.0, std::nullopt, 5.0);
        CHECK (runItem (menu, "Delete bar 2 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-4", "B 4-12" });
        f.model().undo();
        CHECK (layout (*f.keys) == untouched);

        // The marker items are still there.
        CHECK (runItem (f.ruler().createMenu (4.0, std::nullopt), "Verse"));
        CHECK (core::markers::getAll (f.project->getEdit()).size() == 1);
    }

    TEST_CASE ("the ruler's menu: the loop region when the click is in it, and the region follows")
    {
        Fixture f;
        f.model().setLoopRegion ({ 0.0, 8.0 });

        auto menu = f.ruler().createMenu (2.0, std::nullopt, 2.0);
        CHECK (runItem (menu, "Duplicate bars 1-2 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "A 8-16", "B 16-24" });
        CHECK (loopOf (f.model()) == std::pair { 8.0, 16.0 });     // again: once more

        menu = f.ruler().createMenu (10.0, std::nullopt, 10.0);
        CHECK (runItem (menu, "Duplicate bars 3-4 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "A 8-16", "A 16-24", "B 24-32" });
        CHECK (loopOf (f.model()) == std::pair { 16.0, 24.0 });

        menu = f.ruler().createMenu (18.0, std::nullopt, 18.0);
        CHECK (runItem (menu, "Delete bars 5-6 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "A 8-16", "B 16-24" });
        CHECK_FALSE (f.model().getLoopRegion().has_value());

        // Outside the region: the clicked bar; a region after the change moves along.
        f.model().setLoopRegion ({ 16.0, 24.0 });
        menu = f.ruler().createMenu (0.0, std::nullopt, 1.0);
        CHECK (runItem (menu, "Insert an empty bar before bar 1 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 4-12", "A 12-20", "B 20-28" });
        CHECK (loopOf (f.model()) == std::pair { 20.0, 28.0 });

        f.model().setLoopRegion ({ 4.0, 12.0 });
        menu = f.ruler().createMenu (4.0, std::nullopt, 5.0);
        CHECK (runItem (menu, "Insert 2 empty bars before bar 2 (all tracks)"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 12-20", "A 20-28", "B 28-36" });
        CHECK (loopOf (f.model()) == std::pair { 4.0, 12.0 });     // over the new bars
    }

    TEST_CASE ("Ctrl+Insert / Ctrl+Delete: empty bars before the loop region, its bars deleted")
    {
        Fixture f;
        auto& arrangement = f.main.getArrangement();
        const juce::KeyPress ctrlInsert (juce::KeyPress::insertKey, juce::ModifierKeys::ctrlModifier, 0);
        const juce::KeyPress ctrlDelete (juce::KeyPress::deleteKey, juce::ModifierKeys::ctrlModifier, 0);

        // No loop region: it says how to make one, and nothing changes.
        juce::String message;
        arrangement.onMessage = [&message] (const juce::String& text, bool) { message = text; };
        CHECK (arrangement.handleShortcut (ctrlDelete));
        CHECK (message.contains ("loop region"));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "B 8-16" });

        f.model().setLoopRegion ({ 8.0, 12.0 });
        CHECK (arrangement.handleShortcut (ctrlInsert));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "B 12-20" });

        CHECK (arrangement.handleShortcut (ctrlDelete));
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "B 8-16" });

        f.model().undo();
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "B 12-20" });
        f.model().undo();
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "B 8-16" });
    }

    TEST_CASE ("a song from the loop region, from the ruler's menu")
    {
        Fixture f;

        // Outside a loop region: not offered.
        CHECK_FALSE (runItem (f.ruler().createMenu (0.0, std::nullopt, 1.0), "Pop: intro, verse, chorus, verse, chorus, outro"));

        // A 2-bar loop of "A": each part is it four times (8 bars); "B" goes after the song.
        f.model().setLoopRegion ({ 0.0, 8.0 });
        auto menu = f.ruler().createMenu (2.0, std::nullopt, 2.0);
        CHECK (runItem (menu, "Pop: intro, verse, chorus, verse, chorus, outro"));

        const auto clips = layout (*f.keys);
        CHECK (clips.size() == 21);                         // intro 2, verse 4, chorus 4, verse 4, chorus 4, outro 2; and B
        CHECK (clips[20] == "B 160-168");
        CHECK (core::markers::getAll (f.project->getEdit()).size() == 6);
        CHECK_FALSE (f.model().getLoopRegion().has_value());
        CHECK (f.model().getPlayMode() == arrangement::PlayMode::song);

        f.model().undo();
        CHECK (layout (*f.keys) == juce::StringArray { "A 0-8", "B 8-16" });
        CHECK (core::markers::getAll (f.project->getEdit()).empty());
    }

    TEST_CASE ("a song from a loop, as it looks")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        // Four parts in a 4-bar loop.
        auto* drums = project->getTracks().getFirst();
        drums->setName ("Drums");
        const std::vector<std::pair<juce::String, std::vector<int>>> parts { { "Drums", { 36, 42, 38, 42 } }, { "Bass", { 36, 36, 43, 41 } },
                                                                             { "Keys", { 60, 64, 67, 72 } }, { "Lead", { 76, 79, 77, 74 } } };

        for (const auto& [name, pitches] : parts)
        {
            auto* track = name == "Drums" ? drums : project->addTrack (name);
            REQUIRE (track != nullptr);
            auto* clip = project->addMidiClip (*track, 0.0, 16.0, name);
            REQUIRE (clip != nullptr);

            for (int beat = 0; beat < 16; ++beat)
                clip->getSequence().addNote (pitches[(size_t) (beat % 4)], te::BeatPosition::fromBeats (beat), te::BeatDuration::fromBeats (0.9),
                                             100, 0, nullptr);
        }

        auto& model = main.getArrangementModel();
        model.setLoopRegion ({ 0.0, 16.0 });
        CHECK (model.makeSong (core::song_forms::getForms()[0]) == doctest::Approx (160.0));

        main.getArrangement().zoomToFit();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-song-from-loop.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }

    TEST_CASE ("describing bars")
    {
        using arrangement::ArrangementModel;
        CHECK (ArrangementModel::describeBars ({ 16.0, 32.0 }, 4.0) == "bars 5-8");
        CHECK (ArrangementModel::describeBars ({ 8.0, 12.0 }, 4.0) == "bar 3");
        CHECK (ArrangementModel::describeBars ({ 1.5, 4.0 }, 4.0) == "beats 2.50-5");
    }
}
