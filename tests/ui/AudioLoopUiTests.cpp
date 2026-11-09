#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "core/AudioClips.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/audioeditor/AudioEditorModel.h"

using namespace harmonia;
namespace audio_clips = core::audio_clips;
namespace clips = core::clips;

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
}

TEST_SUITE ("ui: looping audio clips and crossfades")
{
    TEST_CASE ("the clip's menu loops it and turns its crossfades off; the arrangement and the editor show the loop")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->addTrack ("Audio");
        REQUIRE (track != nullptr);
        core::tracks::setKind (*track, core::tracks::Kind::audio);

        juce::String error;
        const auto file = test::writeTone (temp.getFile ("Tone.wav"), 2.0, 0.25f);     // 4 beats at 120 BPM
        auto* clip = dynamic_cast<te::AudioClipBase*> (project->addAudioClip (*track, file, 4.0, error));
        REQUIRE (clip != nullptr);
        project->clearUndoHistory();

        auto main = std::make_unique<ui::MainComponent> (test::session(), *project);
        main->setBounds (0, 0, 1280, 800);
        auto& arrangement = main->getArrangement();
        main->getArrangementModel().setCurrentTrack (track);
        dispatch();

        // Crossfades are on for a new clip; the menu turns them off and on, with undo.
        auto menu = arrangement.createClipMenu (*clip);
        REQUIRE (find (menu, "Crossfade where it overlaps another clip").has_value());
        CHECK (find (menu, "Crossfade where it overlaps another clip")->isTicked);
        find (menu, "Crossfade where it overlaps another clip")->action();
        CHECK_FALSE (audio_clips::hasCrossfades (*clip));
        CHECK_FALSE (find (arrangement.createClipMenu (*clip), "Crossfade where it overlaps another clip")->isTicked);
        project->undo();
        CHECK (audio_clips::hasCrossfades (*clip));

        // Loop: what it plays repeats.
        REQUIRE (find (menu, "Loop (repeats when made longer)").has_value());
        CHECK_FALSE (find (menu, "Loop (repeats when made longer)")->isTicked);
        find (menu, "Loop (repeats when made longer)")->action();
        CHECK (audio_clips::isLooping (*clip));
        CHECK (project->getUndoDescription() == "Loop");
        CHECK (find (arrangement.createClipMenu (*clip), "Loop (repeats when made longer)")->isTicked);

        clips::setEnd (*clip, 20.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (20.0));
        CHECK (audio_clips::getLoopLengthBeats (*clip) == doctest::Approx (4.0).epsilon (0.01));
        CHECK (audio_clips::getLoopSongStartBeat (*clip) == doctest::Approx (4.0).epsilon (0.01));
        CHECK (audio_clips::getLoopFileSeconds (*clip).getStart() == doctest::Approx (0.0));
        CHECK (audio_clips::getLoopFileSeconds (*clip).getEnd() == doctest::Approx (2.0).epsilon (0.01));
        dispatch();
        CHECK (main->createComponentSnapshot (main->getLocalBounds()).isValid());

        // The editor shows the part that repeats, as its first time round lies in the song.
        ui::audioeditor::AudioEditorModel editor (main->getArrangementModel());
        REQUIRE (editor.getClip() == clip);
        CHECK (editor.getPlayedSeconds().getStart() == doctest::Approx (0.0));
        CHECK (editor.getPlayedSeconds().getEnd() == doctest::Approx (2.0).epsilon (0.01));
        CHECK (editor.beatToFileSeconds (6.0) == doctest::Approx (1.0).epsilon (0.01));
        CHECK (editor.fileSecondsToBeat (1.0) == doctest::Approx (6.0).epsilon (0.01));

        // Split in the third time round: the later half goes on in step.
        project->beginGesture ("Split");
        auto* right = dynamic_cast<te::AudioClipBase*> (clips::split (*clip, 14.0));
        REQUIRE (right != nullptr);
        CHECK (audio_clips::isLooping (*right));
        CHECK (clips::getStartBeat (*right) == doctest::Approx (14.0));
        const auto firstRound = audio_clips::getLoopSongStartBeat (*right);
        CHECK (std::fmod (14.0 - firstRound + 400.0, 4.0) == doctest::Approx (2.0).epsilon (0.02));   // half way round
        project->undo();

        // Play once again.
        find (arrangement.createClipMenu (*clip), "Loop (repeats when made longer)")->action();
        CHECK_FALSE (audio_clips::isLooping (*clip));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (8.0).epsilon (0.01));

        main.reset();
    }
}
