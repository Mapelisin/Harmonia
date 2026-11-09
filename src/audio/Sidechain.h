#pragma once

#include "core/Project.h"

/** Sidechain: an effect that listens to another track. The classic use is a compressor on the
    bass that ducks whenever the kick hits.

    Tracktion feeds the source track's sound (after its effects and fader, before its mute: a
    muted kick still ducks the bass) into the effect's extra inputs; the built-in compressor then
    reacts to it instead of to its own track. Plugins with a sidechain input get it too. */
namespace harmonia::audio::sidechain
{
    /** The effect has inputs for another track's sound (the built-in compressor, plugins with a
        sidechain input). */
    bool canListen (te::Plugin&);

    /** The tracks the effect can listen to: song tracks, except its own track and any track whose
        sound depends on it (that would go round in a circle). */
    juce::Array<te::AudioTrack*> getPossibleSources (const core::Project&, te::Plugin&);

    /** The track the effect listens to, or nullptr. */
    te::AudioTrack* getSource (te::Plugin&);

    /** Makes the effect listen to a track (one of getPossibleSources), or stop (nullptr). One undo
        step. Returns false if the track can't be a source for it. */
    bool setSource (core::Project&, te::Plugin&, te::AudioTrack*);

    /** Effects listening to `track` stop (it is about to be deleted). Part of the current undo step. */
    void forgetSource (te::AudioTrack& track);

    /** The tracks the effects on these tracks listen to, and the ones the effects on those listen
        to..., not counting these tracks: what a render of just these needs to sound right. */
    juce::Array<te::AudioTrack*> getSourcesFor (const juce::Array<te::AudioTrack*>& tracks);
}
