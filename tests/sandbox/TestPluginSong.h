#pragma once

#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "audio/PluginSafety.h"
#include "core/Project.h"
#include "sandbox/RemotePlugin.h"
#include "sandbox/TestPlugins.h"

namespace harmonia::test::plugins
{
    /** The test plugins are in the engine's list of plugins while it lives (as if scanned). */
    struct KnownTestPlugins
    {
        KnownTestPlugins()
        {
            list().addType (synth());
            list().addType (gain());
        }

        ~KnownTestPlugins()
        {
            list().removeType (synth());
            list().removeType (gain());
        }

        static juce::KnownPluginList& list()    { return session().getEngine().getPluginManager().knownPluginList; }
    };

    /** A song of one track playing the test synth (in a process of its own): a note over its first
        two bars. */
    struct SynthSong
    {
        SynthSong()
            : project (core::Project::createEmpty (session())),
              track (*project->getTracks().getFirst())
        {
            const auto error = audio::instruments::useExternal (track, synth());
            REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

            auto* clip = project->addMidiClip (track, 0.0, 8.0, "Note");
            REQUIRE (clip != nullptr);
            clip->getSequence().addNote (60, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (8.0), 100, 0, nullptr);

            // The engine puts the new clip's node in its place among the track's a moment later, as
            // an undo step. Here, not in the middle of the test's first step: undone with that
            // step, it would be done again by the engine, and the step couldn't be redone.
            for (int i = 0; i < 2000 && project->isSettling(); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

            REQUIRE_FALSE (project->isSettling());
        }

        te::Plugin& instrument()
        {
            auto* plugin = audio::instruments::get (track);
            REQUIRE (plugin != nullptr);
            return *plugin;
        }

        sandbox::RemotePlugin& remote()
        {
            auto* found = audio::plugin_safety::getRemote (instrument());
            REQUIRE (found != nullptr);
            return *found;
        }

        juce::AudioProcessorParameter& parameter (int index)
        {
            auto* found = remote().getParameters()[index];
            REQUIRE (found != nullptr);
            return *found;
        }

        KnownTestPlugins known;
        std::unique_ptr<core::Project> project;
        te::AudioTrack& track;
    };
}
