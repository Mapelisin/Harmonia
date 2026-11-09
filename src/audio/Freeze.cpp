#include "audio/Freeze.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/AudioClips.h"
#include "core/Clips.h"
#include "core/Tracks.h"

namespace harmonia::audio::freeze
{
    namespace
    {
        const juce::Identifier frozenId ("hFrozen");                // on the track's node
        const juce::Identifier mutedByFreezeId ("hFrozenMuted");    // on a clip the freeze muted
        const juce::Identifier offByFreezeId ("hFrozenOff");        // on a plugin the freeze turned off

        te::WaveAudioClip* getFreezeClip (const te::AudioTrack& track)
        {
            for (auto* clip : track.getClips())
                if (core::clips::isFreezeClip (*clip))
                    return dynamic_cast<te::WaveAudioClip*> (clip);

            return nullptr;
        }

        /** The track's instrument and effects: what the frozen file has in it already. */
        juce::Array<te::Plugin*> getSoundPlugins (te::AudioTrack& track)
        {
            auto plugins = effects::get (track.pluginList);

            if (auto* instrument = instruments::get (track))
                plugins.insert (0, instrument);

            return plugins;
        }
    }

    bool isFrozen (const te::AudioTrack& track)
    {
        return (bool) track.state[frozenId];
    }

    bool canFreeze (te::AudioTrack& track)
    {
        return ! isFrozen (track) && ! core::tracks::isBus (track) && ! core::clips::getClips (track).isEmpty();
    }

    juce::File getFile (const te::AudioTrack& track)
    {
        auto* clip = getFreezeClip (track);
        return clip != nullptr ? clip->getSourceFileReference().getFile() : juce::File();
    }

    juce::String freeze (core::Project& project, te::AudioTrack& track)
    {
        if (isFrozen (track))
            return {};

        const auto clips = core::clips::getClips (track);

        if (core::tracks::isBus (track) || clips.isEmpty())
            return "Nothing to freeze: " + track.getName() + " has no clips";

        auto start = std::numeric_limits<double>::max(), end = 0.0;

        for (auto* clip : clips)
        {
            start = std::min (start, core::clips::getStartBeat (*clip));
            end = std::max (end, core::clips::getEndBeat (*clip));
        }

        // From its first clip to its last, and a moment for the sound to ring out.
        auto& edit = track.edit;
        const auto& tempo = edit.tempoSequence;
        const te::TimeRange range (tempo.toTime (te::BeatPosition::fromBeats (start)),
                                   tempo.toTime (te::BeatPosition::fromBeats (end)) + te::TimeDuration::fromSeconds (exportTailSeconds));
        const auto file = edit.engine.getEngineBehaviour().getFileForNewAudioRecording (track, ".wav");

        if (const auto error = renderTrack (track, file, range, true); error.isNotEmpty())
            return error;

        auto* undoManager = &edit.getUndoManager();
        project.beginGesture ("Freeze track");

        // The file plays instead of the clips and what they went through.
        for (auto* clip : clips)
        {
            if (! clip->isMuted())
            {
                clip->state.setProperty (mutedByFreezeId, true, undoManager);
                clip->setMuted (true);
            }
        }

        for (auto* plugin : getSoundPlugins (track))
        {
            if (plugin->isEnabled())
            {
                plugin->state.setProperty (offByFreezeId, true, undoManager);
                plugin->setEnabled (false);
            }
        }

        juce::String error;
        auto* frozen = project.addAudioClip (track, file, start, error);

        if (frozen == nullptr)
        {
            // As it was: the half-made step taken back.
            project.undo();
            return error.isNotEmpty() ? error : juce::String ("Couldn't freeze " + track.getName());
        }

        core::clips::setFreezeClip (*frozen);

        // Before the track's other clips, where the engine keeps the earliest: it would put it
        // there itself a moment later, as an undo step of its own.
        if (auto parent = frozen->state.getParent(); parent.isValid())
            for (int i = 0; i < parent.getNumChildren(); ++i)
                if (te::Clip::isClipState (parent.getChild (i)))
                {
                    parent.moveChild (parent.indexOf (frozen->state), i, undoManager);
                    break;
                }

        frozen->setName (track.getName() + " (frozen)");
        core::audio_clips::setCrossfades (*frozen, false);
        track.state.setProperty (frozenId, true, undoManager);
        return {};
    }

    void unfreeze (core::Project& project, te::AudioTrack& track)
    {
        if (! isFrozen (track))
            return;

        auto* undoManager = &track.edit.getUndoManager();
        project.beginGesture ("Unfreeze track");

        if (te::Clip::Ptr frozen = getFreezeClip (track))
            frozen->removeFromParent();

        for (auto* clip : core::clips::getClips (track))
        {
            if (clip->state.hasProperty (mutedByFreezeId))
            {
                clip->state.removeProperty (mutedByFreezeId, undoManager);
                clip->setMuted (false);
            }

            // The frozen file lay over them: their fades are their own again.
            if (auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip))
                audioClip->updateAutoCrossfadesAsync (false);
        }

        for (auto* plugin : track.pluginList)
        {
            if (plugin->state.hasProperty (offByFreezeId))
            {
                plugin->state.removeProperty (offByFreezeId, undoManager);
                plugin->setEnabled (true);
            }
        }

        track.state.removeProperty (frozenId, undoManager);
    }
}
