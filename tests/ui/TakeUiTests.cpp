#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Takes.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/TakeMenu.h"

using namespace harmonia;
namespace takes = core::takes;

namespace
{
    void dispatch (int ms = 50)
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

    void choose (const juce::PopupMenu& menu, const juce::String& text)
    {
        const auto item = find (menu, text);
        REQUIRE_MESSAGE (item.has_value(), text.toStdString());
        REQUIRE_MESSAGE (item->isEnabled, text.toStdString());
        REQUIRE (item->action != nullptr);
        item->action();
    }

    /** A song with an audio track whose clip has three takes of 2 seconds (as if recorded round a
        loop of 4 beats), playing the last. */
    struct TakesFixture
    {
        TakesFixture()
        {
            track = project->addTrack ("Vocals");
            REQUIRE (track != nullptr);
            core::tracks::setKind (*track, core::tracks::Kind::audio);

            juce::String error;
            const auto first = test::writeTone (temp.getFile ("Vocals.wav"), 2.0, 0.25f);
            auto* wave = dynamic_cast<te::WaveAudioClip*> (project->addAudioClip (*track, first, 4.0, error));
            REQUIRE (wave != nullptr);

            // What the engine leaves after a recording round a loop.
            wave->addTake (first);
            wave->addTake (test::writeTone (temp.getFile ("Vocals_take_2.wav"), 2.0, 0.3f));
            wave->addTake (test::writeTone (temp.getFile ("Vocals_take_3.wav"), 2.0, 0.35f));
            REQUIRE (takes::adoptRecorded (project->getEdit()) == 1);

            clip = wave;
            dispatch();
            project->clearUndoHistory();
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* track = nullptr;
        te::AudioClipBase* clip = nullptr;
    };
}

TEST_SUITE ("ui: takes")
{
    TEST_CASE ("the takes menu: the one playing ticked, choosing is an undo step, keeping only one")
    {
        TakesFixture f;
        REQUIRE (takes::getCount (*f.clip) == 3);
        CHECK (takes::getCurrent (*f.clip) == 2);
        CHECK (ui::take_menu::describe (*f.clip) == "Take 3 of 3");

        const auto menu = ui::take_menu::make (*f.project, *f.clip);
        CHECK (find (menu, "Take 3")->isTicked);
        CHECK_FALSE (find (menu, "Take 1")->isTicked);
        CHECK (find (menu, "Take 1")->text.contains ("2.0 s"));

        choose (menu, "Take 1");
        CHECK (takes::getCurrent (*f.clip) == 0);
        CHECK (ui::take_menu::describe (*f.clip) == "Take 1 of 3");
        CHECK (f.project->getUndoDescription() == "Take 1");
        CHECK (find (ui::take_menu::make (*f.project, *f.clip), "Take 1")->isTicked);

        f.project->undo();
        CHECK (takes::getCurrent (*f.clip) == 2);
        f.project->redo();
        CHECK (takes::getCurrent (*f.clip) == 0);

        // A take whose file is gone can't be chosen.
        takes::getFile (*f.clip, 1).deleteFile();
        CHECK_FALSE (find (ui::take_menu::make (*f.project, *f.clip), "Take 2")->isEnabled);

        choose (ui::take_menu::make (*f.project, *f.clip), "Keep only this take");
        CHECK (takes::getCount (*f.clip) == 0);
        CHECK (ui::take_menu::describe (*f.clip).isEmpty());
        CHECK (ui::take_menu::make (*f.project, *f.clip).getNumItems() == 0);
        CHECK (f.clip->getSourceFileReference().getFile() == f.temp.getFile ("Vocals.wav"));
        f.project->undo();
        CHECK (takes::getCount (*f.clip) == 3);
    }

    TEST_CASE ("main window: the clip's menu and the audio editor have the takes; a loop region in Song mode records takes")
    {
        TakesFixture f;
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& model = main->getArrangementModel();
        model.setCurrentTrack (f.track);
        dispatch();

        const auto clipMenu = main->getArrangement().createClipMenu (*f.clip);
        REQUIRE (find (clipMenu, "Takes (Take 3 of 3)").has_value());
        choose (clipMenu, "Take 2");
        CHECK (takes::getCurrent (*f.clip) == 1);

        dispatch (150);
        auto& button = main->getAudioEditor().getTakesButton();
        CHECK (button.isVisible());
        CHECK (button.getButtonText() == "Take 2 of 3");
        CHECK (button.getWidth() > 0);

        // A clip without takes: no button, no submenu.
        f.project->beginGesture ("Keep");
        takes::keepOnlyCurrent (*f.clip);
        dispatch (150);
        main->getAudioEditor().refresh();
        CHECK_FALSE (button.isVisible());
        CHECK_FALSE (find (main->getArrangement().createClipMenu (*f.clip), "Takes").has_value());

        // Takes are recorded round the loop region of Song mode only.
        auto& transport = main->getTransport();
        main->setPlayMode (ui::arrangement::PlayMode::song);
        CHECK_FALSE (transport.recordsTakes());
        model.setLoopRegion ({ 4.0, 8.0 });
        dispatch();
        CHECK (transport.recordsTakes());
        main->setPlayMode (ui::arrangement::PlayMode::clip);
        CHECK_FALSE (transport.recordsTakes());

        main.reset();
    }
}
