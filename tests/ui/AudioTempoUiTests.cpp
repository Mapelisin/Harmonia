#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"

using namespace harmonia;
namespace audio_tempo = core::audio_tempo;
namespace clips = core::clips;

namespace
{
    /** The main window of an empty song at 120 BPM. */
    struct Window
    {
        Window()
        {
            project->clearUndoHistory();
            main->setBounds (0, 0, 1280, 800);
        }

        ~Window()
        {
            main.reset();
            project.reset();
        }

        /** Drops `file` below the tracks at `beat`: the clip it becomes. */
        te::WaveAudioClip* drop (const juce::File& file, double beat = 0.0)
        {
            auto& model = main->getArrangementModel();
            REQUIRE (model.importFiles ({ file }, -1, beat).isEmpty());
            auto selected = model.getSelectedClips();
            REQUIRE (selected.size() == 1);
            return dynamic_cast<te::WaveAudioClip*> (selected.getFirst());
        }

        juce::PopupMenu menuFor (te::Clip& clip)    { return main->getArrangement().createClipMenu (clip); }

        test::TempFolder temp;   // first: deleted last
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main = std::make_unique<ui::MainComponent> (test::session(), *project);
    };

    const juce::PopupMenu::Item* findItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == text)
                return &it.getItem();

        return nullptr;
    }

    bool runItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        if (const auto* item = findItem (menu, text); item != nullptr && item->isEnabled && item->action != nullptr)
        {
            const auto action = item->action;
            action();
            return true;
        }

        return false;
    }
}

TEST_SUITE ("ui: clips follow the song tempo")
{
    TEST_CASE ("a dropped loop follows the song's tempo by itself; a sound that isn't a loop doesn't")
    {
        Window w;

        // 4.8 seconds called "100 BPM": 8 beats of a loop, now 8 beats of the song (4 seconds at 120).
        auto* loop = w.drop (test::writeTone (w.temp.getFile ("Beat 100 BPM.wav"), 4.8, 0.5f, 1, true), 4.0);
        REQUIRE (loop != nullptr);
        CHECK (audio_tempo::getLoopBpm (*loop).value_or (0.0) == doctest::Approx (100.0).epsilon (0.002));
        CHECK (clips::getStartBeat (*loop) == doctest::Approx (4.0));
        CHECK (clips::getEndBeat (*loop) == doctest::Approx (12.0).epsilon (0.002));

        auto* pad = w.drop (test::writeTone (w.temp.getFile ("Pad.wav"), 3.0));
        REQUIRE (pad != nullptr);
        CHECK_FALSE (audio_tempo::followsTempo (*pad));
        CHECK (pad->getPosition().getLength().inSeconds() == doctest::Approx (3.0));

        // One undo takes the drop back, following and all.
        w.project->getEdit().dispatchPendingUpdatesSynchronously();
        w.project->undo();
        w.project->undo();
        CHECK (w.main->getArrangementModel().getNumTracks() == 1);
    }

    TEST_CASE ("the clip menu: follow the tempo or not, the loop's tempo, the pitch")
    {
        Window w;
        auto* clip = w.drop (test::writeTone (w.temp.getFile ("Beat 100 BPM.wav"), 4.8, 0.5f, 1, true));
        REQUIRE (clip != nullptr);

        const juce::String following = "Follow the song tempo (a loop at 100 BPM)";
        const auto menu = w.menuFor (*clip);
        const auto* item = findItem (menu, following);
        REQUIRE (item != nullptr);
        CHECK (item->isTicked);

        // The loop at half the tempo plays twice as fast: 4 beats.
        CHECK (runItem (w.menuFor (*clip), "Half (50 BPM: plays twice as fast)"));
        CHECK (audio_tempo::getLoopBpm (*clip).value_or (0.0) == doctest::Approx (50.0).epsilon (0.002));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (4.0).epsilon (0.002));
        CHECK (w.project->getUndoDescription() == "Loop tempo");
        w.project->getEdit().dispatchPendingUpdatesSynchronously();
        w.project->undo();
        CHECK (audio_tempo::getLoopBpm (*clip).value_or (0.0) == doctest::Approx (100.0).epsilon (0.002));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (8.0).epsilon (0.002));

        // Off: its own speed, 4.8 seconds. On again: the tempo in its name.
        CHECK (runItem (w.menuFor (*clip), following));
        CHECK_FALSE (audio_tempo::followsTempo (*clip));
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (4.8).epsilon (0.002));
        CHECK (findItem (w.menuFor (*clip), "Follow the song tempo") != nullptr);
        CHECK_FALSE (findItem (w.menuFor (*clip), "Follow the song tempo")->isTicked);
        CHECK_FALSE (findItem (w.menuFor (*clip), "Loop tempo")->isEnabled);

        CHECK (runItem (w.menuFor (*clip), "Follow the song tempo"));
        CHECK (audio_tempo::getLoopBpm (*clip).value_or (0.0) == doctest::Approx (100.0).epsilon (0.002));

        // Higher by an octave, then back.
        CHECK (runItem (w.menuFor (*clip), "Up an octave"));
        CHECK (audio_tempo::getPitch (*clip) == doctest::Approx (12.0f));
        CHECK (findItem (w.menuFor (*clip), "Up an octave")->isEnabled);
        CHECK (runItem (w.menuFor (*clip), "Up an octave"));
        CHECK (audio_tempo::getPitch (*clip) == doctest::Approx (24.0f));
        CHECK_FALSE (findItem (w.menuFor (*clip), "Up a semitone")->isEnabled);
        CHECK (runItem (w.menuFor (*clip), "Original pitch"));
        CHECK (audio_tempo::getPitch (*clip) == doctest::Approx (0.0f));
        CHECK (runItem (w.menuFor (*clip), "Down a semitone"));
        CHECK (audio_tempo::getPitch (*clip) == doctest::Approx (-1.0f));

        // The clip says what it does after its name; the window paints it.
        CHECK (w.main->createComponentSnapshot (w.main->getLocalBounds()).isValid());
    }

    TEST_CASE ("a sound that isn't a loop can follow too: it is taken to be at the song's tempo")
    {
        Window w;
        auto* clip = w.drop (test::writeTone (w.temp.getFile ("Chords.wav"), 4.0));
        REQUIRE (clip != nullptr);
        CHECK_FALSE (audio_tempo::followsTempo (*clip));

        CHECK (runItem (w.menuFor (*clip), "Follow the song tempo"));
        CHECK (audio_tempo::getLoopBpm (*clip).value_or (0.0) == doctest::Approx (120.0));
        CHECK (clip->getPosition().getLength().inSeconds() == doctest::Approx (4.0));

        // Slower song: the chords slow down with it and stay 8 beats long.
        w.project->beginGesture ("Change tempo");
        w.project->setTempoBpm (90.0);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (8.0));
        CHECK (core::audio_tempo::getPlaybackSpeed (*clip) == doctest::Approx (0.75));
    }

    TEST_CASE ("tempi are written without needless decimals")
    {
        CHECK (audio_tempo::formatBpm (95.0) == "95");
        CHECK (audio_tempo::formatBpm (94.999) == "95");
        CHECK (audio_tempo::formatBpm (92.5) == "92.5");
        CHECK (audio_tempo::formatBpm (127.94) == "127.9");
    }
}
