#include <doctest.h>

#include "TestAudioRig.h"
#include "audio/Effects.h"
#include "audio/Freeze.h"
#include "audio/Instruments.h"
#include "audio/Transport.h"
#include "core/AudioClips.h"
#include "core/Clips.h"
#include "core/Project.h"

using namespace harmonia;
namespace freeze = audio::freeze;
namespace clips = core::clips;

namespace
{
    void settle (te::Edit& edit)
    {
        edit.dispatchPendingUpdatesSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    /** The level heard over seconds 0.3 to 1.3 of the song. */
    float play (test::AudioRig& rig)
    {
        auto& edit = rig.project->getEdit();
        settle (edit);
        audio::Transport transport (edit);
        transport.setPositionSeconds (0.0);
        transport.play();
        rig.process (0.3, 0.0f);
        const auto level = rig.process (1.0, 0.0f);
        transport.stop();
        return level;
    }

    /** The song's first track with the built-in synth holding a note for two bars, through a filter. */
    te::AudioTrack& makeSynthTrack (test::AudioRig& rig)
    {
        auto& track = *rig.project->getTracks().getFirst();
        track.setName ("Synth");
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        auto* clip = rig.project->addMidiClip (track, 0.0, 8.0, "Notes");
        REQUIRE (clip != nullptr);
        clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (8.0), 100, 0, nullptr);

        juce::String error;

        for (const auto& type : audio::effects::getBuiltIn())
            if (type.name == "EQ")
                REQUIRE (audio::effects::add (track.pluginList, type, error) != nullptr);

        rig.project->clearUndoHistory();
        return track;
    }
}

TEST_SUITE ("audio: freezing tracks")
{
    TEST_CASE ("a frozen track plays its file with the instrument and effects off, and sounds the same; undo unfreezes")
    {
        test::AudioRig rig;
        auto& track = makeSynthTrack (rig);
        auto* synth = audio::instruments::get (track);
        auto* eq = audio::effects::get (track.pluginList).getFirst();
        auto* notes = clips::getClips (track).getFirst();
        REQUIRE (synth != nullptr);
        REQUIRE (eq != nullptr);

        // Quieter than full, to see the fader isn't counted twice.
        track.getVolumePlugin()->setVolumeDb (-12.0f);
        const auto live = play (rig);
        REQUIRE (live > 0.005f);

        CHECK (freeze::canFreeze (track));
        CHECK_FALSE (freeze::isFrozen (track));
        const auto error = freeze::freeze (*rig.project, track);
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        CHECK (freeze::isFrozen (track));
        CHECK_FALSE (freeze::canFreeze (track));
        CHECK (freeze::getFile (track).existsAsFile());
        CHECK (freeze::getFile (track).isAChildOf (rig.temp.getFile ("Recordings")));
        CHECK_FALSE (synth->isEnabled());
        CHECK_FALSE (eq->isEnabled());
        CHECK (notes->isMuted());
        CHECK (track.getVolumePlugin()->isEnabled());
        CHECK (rig.project->getUndoDescription() == "Freeze track");

        // The file isn't one of the track's clips: nothing shows or edits it.
        REQUIRE (clips::getClips (track).size() == 1);
        CHECK (clips::getClips (track).getFirst() == notes);

        const auto frozen = play (rig);
        CHECK (frozen == doctest::Approx (live).epsilon (0.15));

        // Frozen twice changes nothing.
        CHECK (freeze::freeze (*rig.project, track).isEmpty());
        CHECK (rig.project->getUndoDescription() == "Freeze track");

        rig.project->undo();
        CHECK_FALSE (freeze::isFrozen (track));
        CHECK (synth->isEnabled());
        CHECK (eq->isEnabled());
        CHECK_FALSE (notes->isMuted());
        CHECK (play (rig) == doctest::Approx (live).epsilon (0.15));

        rig.project->redo();
        CHECK (freeze::isFrozen (track));
        CHECK_FALSE (synth->isEnabled());

        // The fader still works on it.
        track.getVolumePlugin()->setVolumeDb (-100.0f);
        CHECK (play (rig) < live * 0.05f);
        track.getVolumePlugin()->setVolumeDb (-12.0f);
    }

    TEST_CASE ("unfreezing puts back only what the freeze turned off")
    {
        test::AudioRig rig;
        auto& track = makeSynthTrack (rig);
        auto* synth = audio::instruments::get (track);
        auto* eq = audio::effects::get (track.pluginList).getFirst();

        // A bypassed effect and a muted clip stay so.
        audio::effects::setBypassed (*eq, true);
        auto* silent = rig.project->addMidiClip (track, 8.0, 4.0, "Muted");
        REQUIRE (silent != nullptr);
        silent->setMuted (true);
        rig.project->clearUndoHistory();

        REQUIRE (freeze::freeze (*rig.project, track).isEmpty());
        CHECK_FALSE (synth->isEnabled());

        freeze::unfreeze (*rig.project, track);
        CHECK_FALSE (freeze::isFrozen (track));
        CHECK (rig.project->getUndoDescription() == "Unfreeze track");
        CHECK (synth->isEnabled());
        CHECK_FALSE (eq->isEnabled());
        CHECK (silent->isMuted());
        CHECK_FALSE (clips::getClips (track).getFirst()->isMuted());
        CHECK (clips::getClips (track).size() == 2);
        CHECK (track.getClips().size() == 2);       // the frozen file's clip is gone

        rig.project->undo();
        CHECK (freeze::isFrozen (track));

        // A track without clips, or a return track, isn't frozen.
        auto* empty = rig.project->addTrack ("Empty");
        REQUIRE (empty != nullptr);
        CHECK_FALSE (freeze::canFreeze (*empty));
        CHECK (freeze::freeze (*rig.project, *empty).isNotEmpty());
    }

    TEST_CASE ("an audio track freezes too, and a frozen track is saved frozen")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto& track = rig.addAudioTrack ("Vocals");
        const auto tone = test::writeTone (rig.temp.getFile ("Tone.wav"), 4.0, 0.25f);
        juce::String error;
        auto* clip = dynamic_cast<te::AudioClipBase*> (rig.project->addAudioClip (track, tone, 0.0, error));
        REQUIRE (clip != nullptr);
        clips::setFadeInBeats (*clip, 0.5);
        settle (edit);

        const auto live = play (rig);
        REQUIRE (live > 0.05f);
        REQUIRE (freeze::freeze (*rig.project, track).isEmpty());
        CHECK (clip->isMuted());
        CHECK (play (rig) == doctest::Approx (live).epsilon (0.1));

        const auto file = rig.temp.getFile ("Song.harmonia");
        REQUIRE (rig.project->save (file).isEmpty());
        auto loaded = core::Project::load (rig.session, file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        te::AudioTrack* loadedTrack = nullptr;

        for (auto* t : loaded->getTracks())
            if (t->getName() == "Vocals")
                loadedTrack = t;

        REQUIRE (loadedTrack != nullptr);
        CHECK (freeze::isFrozen (*loadedTrack));
        CHECK (freeze::getFile (*loadedTrack).existsAsFile());
        CHECK (clips::getClips (*loadedTrack).size() == 1);
        loaded.reset();

        // Unfrozen: its own fade is its own again (the file lay over the clip).
        freeze::unfreeze (*rig.project, track);
        settle (edit);
        CHECK_FALSE (clip->isMuted());
        CHECK (clips::getFadeInBeats (*clip) == doctest::Approx (0.5).epsilon (0.02));
        CHECK (clips::getFadeOutBeats (*clip) == doctest::Approx (0.0).epsilon (0.02));
        CHECK (play (rig) == doctest::Approx (live).epsilon (0.1));
    }
}
