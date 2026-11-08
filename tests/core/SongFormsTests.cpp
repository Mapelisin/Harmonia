#include <doctest.h>

#include "TestSession.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/Markers.h"
#include "core/Project.h"
#include "core/SongForms.h"
#include "core/Tracks.h"

using namespace harmonia;
using namespace harmonia::core;
namespace forms = core::song_forms;

namespace
{
    /** A 4-bar loop (16 beats, 4/4): drums, a bass, chords and a lead, a clip each; the chords go on
        after it ("Later"); a marker at its start. */
    struct Loop
    {
        Loop()
        {
            drums = project->getTracks().getFirst();
            drums->setName ("Drums");
            bass = project->addTrack ("Bass");
            keys = project->addTrack ("Keys");
            lead = project->addTrack ("Lead");

            for (auto [track, pitches] : { std::pair { drums, std::vector<int> { 36, 38, 42 } }, std::pair { bass, std::vector<int> { 36, 43 } },
                                           std::pair { keys, std::vector<int> { 60, 64, 67 } }, std::pair { lead, std::vector<int> { 76, 79 } } })
            {
                REQUIRE (track != nullptr);
                auto* clip = project->addMidiClip (*track, 0.0, 16.0, track->getName());
                REQUIRE (clip != nullptr);

                for (auto pitch : pitches)
                    clip->getSequence().addNote (pitch, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            }

            REQUIRE (project->addMidiClip (*keys, 16.0, 4.0, "Later") != nullptr);
            markers::add (edit(), 0.0, "Idea");
            project->clearUndoHistory();
        }

        te::Edit& edit()    { return project->getEdit(); }

        static std::vector<double> startsOf (te::AudioTrack& track, const juce::String& name)
        {
            std::vector<double> starts;

            for (auto* clip : clips::getClips (track))
                if (clip->getName() == name)
                    starts.push_back (std::round (clips::getStartBeat (*clip) * 1000.0) / 1000.0);

            return starts;
        }

        juce::StringArray markerList()
        {
            juce::StringArray result;

            for (const auto& m : markers::getAll (edit()))
                result.add (m.name + "@" + juce::String (std::round (m.beat * 1000.0) / 1000.0));

            return result;
        }

        std::unique_ptr<Project> project = Project::createEmpty (test::session());
        te::AudioTrack* drums = nullptr;
        te::AudioTrack* bass = nullptr;
        te::AudioTrack* keys = nullptr;
        te::AudioTrack* lead = nullptr;
    };

    juce::String rolesOf (te::Edit& edit, double start, double end)
    {
        juce::StringArray result;

        for (const auto& [track, role] : forms::getRoles (edit, start, end))
            result.add (track->getName() + ": " + forms::getName (role));

        result.sort (false);
        return result.joinIntoString (", ");
    }
}

TEST_SUITE ("core::song_forms")
{
    TEST_CASE ("what each track plays: drums, the bass under C3, the highest of a few the lead, the rest harmony")
    {
        Loop loop;
        CHECK (rolesOf (loop.edit(), 0.0, 16.0) == "Bass: bass, Drums: drums, Keys: harmony, Lead: lead");

        // Only chords and a bass: no lead.
        loop.lead->setName ("Pad");
        loop.lead->getClips()[0]->removeFromParent();
        CHECK (rolesOf (loop.edit(), 0.0, 16.0) == "Bass: bass, Drums: drums, Keys: harmony");

        // A high "bass" isn't one.
        auto* clip = dynamic_cast<te::MidiClip*> (loop.bass->getClips()[0]);
        REQUIRE (clip != nullptr);
        clip->getSequence().clear (nullptr);
        clip->getSequence().addNote (72, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
        CHECK (rolesOf (loop.edit(), 0.0, 16.0) == "Bass: lead, Drums: drums, Keys: harmony");

        // Nothing there: no roles.
        CHECK (forms::getRoles (loop.edit(), 40.0, 48.0).empty());
    }

    TEST_CASE ("a section plays the loop about 8 bars' worth")
    {
        const forms::Section intro { "Intro", 0.5, {} }, verse { "Verse", 1.0, {} };
        CHECK (forms::getLoops (intro, 4.0) == 1);
        CHECK (forms::getLoops (verse, 4.0) == 2);
        CHECK (forms::getLoops (intro, 2.0) == 2);
        CHECK (forms::getLoops (verse, 1.0) == 8);
        CHECK (forms::getLoops (intro, 8.0) == 1);
        CHECK (forms::getLoops (verse, 16.0) == 1);

        CHECK (forms::getForms().size() == 3);

        for (const auto& form : forms::getForms())
            CHECK (form.sections.size() >= 5);
    }

    TEST_CASE ("a pop song from the loop: sections with their parts, markers, linked copies, one undo step")
    {
        Loop loop;
        const auto& pop = forms::getForms()[0];
        loop.project->beginGesture ("Make a song");
        CHECK (forms::build (loop.edit(), 0.0, 16.0, pop, 4.0) == doctest::Approx (160.0));

        // Intro 1 loop, verse 2, chorus 2, verse 2, chorus 2, outro 1.
        CHECK (loop.markerList() == juce::StringArray { "Intro@0", "Verse@16", "Chorus@48", "Verse@80", "Chorus@112", "Outro@144" });
        CHECK (Loop::startsOf (*loop.keys, "Keys") == std::vector<double> { 0, 16, 32, 48, 64, 80, 96, 112, 128, 144 });
        CHECK (Loop::startsOf (*loop.drums, "Drums") == std::vector<double> { 16, 32, 48, 64, 80, 96, 112, 128 });
        CHECK (Loop::startsOf (*loop.bass, "Bass") == std::vector<double> { 16, 32, 48, 64, 80, 96, 112, 128 });
        CHECK (Loop::startsOf (*loop.lead, "Lead") == std::vector<double> { 48, 64, 112, 128, 144 });
        CHECK (Loop::startsOf (*loop.keys, "Later") == std::vector<double> { 160 });

        // Linked: changing the loop's chords changes every copy.
        auto* first = dynamic_cast<te::MidiClip*> (clips::getClips (*loop.keys)[0]);
        REQUIRE (first != nullptr);
        CHECK (clip_links::getGroup (*first).size() == 10);

        loop.project->undo();
        CHECK (loop.markerList() == juce::StringArray { "Idea@0" });
        CHECK (Loop::startsOf (*loop.keys, "Keys") == std::vector<double> { 0 });
        CHECK (Loop::startsOf (*loop.drums, "Drums") == std::vector<double> { 0 });
        CHECK (Loop::startsOf (*loop.keys, "Later") == std::vector<double> { 16 });
    }

    TEST_CASE ("a dance song; a section none of whose parts is there plays them all; an empty loop makes nothing")
    {
        Loop loop;
        const auto& dance = forms::getForms()[1];
        loop.project->beginGesture ("Make a song");

        // An 8-bar loop: one time round a part.
        REQUIRE (loop.project->addMidiClip (*loop.drums, 16.0, 16.0, "Drums 2") != nullptr);
        loop.keys->getClips()[1]->removeFromParent();      // no "Later"
        CHECK (forms::build (loop.edit(), 0.0, 32.0, dance, 4.0) == doctest::Approx (6 * 32.0));
        CHECK (loop.markerList() == juce::StringArray { "Intro@0", "Build@32", "Drop@64", "Break@96", "Drop@128", "Outro@160" });
        CHECK (Loop::startsOf (*loop.drums, "Drums 2") == std::vector<double> { 16, 80, 144, 176 });
        CHECK (Loop::startsOf (*loop.lead, "Lead") == std::vector<double> { 32, 64, 96, 128 });

        // Only drums: the intro has no harmony to play, so the drums play it.
        Loop drumsOnly;

        for (auto* track : { drumsOnly.bass, drumsOnly.keys, drumsOnly.lead })
            for (auto* clip : clips::getClips (*track))
                clip->removeFromParent();

        drumsOnly.project->beginGesture ("Make a song");
        CHECK (forms::build (drumsOnly.edit(), 0.0, 16.0, forms::getForms()[2], 4.0) > 0.0);
        CHECK (Loop::startsOf (*drumsOnly.drums, "Drums").front() == doctest::Approx (0.0));

        // Nothing in the loop.
        CHECK (forms::build (drumsOnly.edit(), 1000.0, 1016.0, forms::getForms()[0], 4.0) == 0.0);
    }
}
