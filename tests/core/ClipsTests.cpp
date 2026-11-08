#include <doctest.h>

#include "TestProject.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"

using namespace harmonia;
namespace clips = core::clips;
namespace links = core::clip_links;

namespace
{
    core::Note makeNote (int pitch, double start, double length = 1.0)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }

    /** Notes of a clip in song beats. */
    std::vector<double> songStarts (const te::MidiClip& clip)
    {
        std::vector<double> starts;

        for (auto* n : clip.getSequence().getNotes())
            starts.push_back (clips::getContentStartBeat (clip) + n->getStartBeat().inBeats());

        return starts;
    }
}

TEST_SUITE ("core::clips arrangement")
{
    TEST_CASE ("a clip moves along the song and to another track with its notes")
    {
        test::ProjectWithClip p;
        core::NoteEditor (*p.clip).add (makeNote (60, 1.0));

        p.project->beginGesture ("Add track");
        auto* second = p.project->addTrack ("Second");
        REQUIRE (second != nullptr);

        p.project->beginGesture ("Move");
        clips::move (*p.clip, 8.0, *second);

        CHECK (clips::getMidiClips (*p.track).isEmpty());
        REQUIRE (clips::getMidiClips (*second).size() == 1);
        auto* moved = clips::getMidiClips (*second).getFirst();
        CHECK (clips::getStartBeat (*moved) == doctest::Approx (8.0));
        CHECK (clips::getEndBeat (*moved) == doctest::Approx (24.0));
        CHECK (songStarts (*moved) == std::vector<double> { 9.0 });

        // Not before the song's start.
        clips::move (*moved, -5.0, *second);
        CHECK (clips::getStartBeat (*moved) == doctest::Approx (0.0));

        p.project->undo();
        REQUIRE (clips::getMidiClips (*p.track).size() == 1);
        CHECK (clips::getStartBeat (*clips::getMidiClips (*p.track).getFirst()) == doctest::Approx (0.0));
        CHECK (clips::getMidiClips (*second).isEmpty());
    }

    TEST_CASE ("trimming a clip's edges hides notes without moving them")
    {
        test::ProjectWithClip p;
        core::NoteEditor (*p.clip).add ({ makeNote (60, 1.0), makeNote (62, 6.0) });

        p.project->beginGesture ("Trim");
        clips::setStart (*p.clip, 4.0, 1.0);
        CHECK (clips::getStartBeat (*p.clip) == doctest::Approx (4.0));
        CHECK (clips::getContentStartBeat (*p.clip) == doctest::Approx (0.0));
        CHECK (songStarts (*p.clip) == std::vector<double> { 1.0, 6.0 });

        // The start can come back, but not before the content's start.
        clips::setStart (*p.clip, -3.0, 1.0);
        CHECK (clips::getStartBeat (*p.clip) == doctest::Approx (0.0));

        // At least the minimum length.
        clips::setStart (*p.clip, 30.0, 1.0);
        CHECK (clips::getStartBeat (*p.clip) == doctest::Approx (15.0));
        clips::setStart (*p.clip, 0.0, 1.0);

        clips::setEnd (*p.clip, 8.0, 1.0);
        CHECK (clips::getEndBeat (*p.clip) == doctest::Approx (8.0));
        clips::setEnd (*p.clip, -2.0, 1.0);
        CHECK (clips::getEndBeat (*p.clip) == doctest::Approx (1.0));
        clips::setEnd (*p.clip, 32.0, 1.0);
        CHECK (clips::getEndBeat (*p.clip) == doctest::Approx (32.0));
        CHECK (songStarts (*p.clip) == std::vector<double> { 1.0, 6.0 });
    }

    TEST_CASE ("the song ends where its last clip ends")
    {
        test::ProjectWithClip p;
        CHECK (clips::getSongEndBeat (p.project->getEdit()) == doctest::Approx (16.0));

        auto* track = p.project->addTrack ("Two");
        p.project->addMidiClip (*track, 20.0, 8.0);
        CHECK (clips::getSongEndBeat (p.project->getEdit()) == doctest::Approx (28.0));

        auto empty = core::Project::createEmpty (test::session());
        CHECK (clips::getSongEndBeat (empty->getEdit()) == doctest::Approx (0.0));
    }

    TEST_CASE ("clips come in time order and know when they are deleted")
    {
        test::ProjectWithClip p;
        auto* late = p.project->addMidiClip (*p.track, 32.0, 4.0, "late");
        auto* early = p.project->addMidiClip (*p.track, 16.0, 4.0, "early");

        const auto inOrder = clips::getMidiClips (*p.track);
        REQUIRE (inOrder.size() == 3);
        CHECK (inOrder[0] == p.clip);
        CHECK (inOrder[1] == early);
        CHECK (inOrder[2] == late);

        const te::Clip::Ptr keep (late);
        CHECK (clips::isInEdit (*late));
        p.project->beginGesture ("Delete");
        clips::remove ({ late });
        CHECK_FALSE (clips::isInEdit (*late));
        CHECK (clips::getMidiClips (*p.track).size() == 2);
    }

    TEST_CASE ("deleting clips dissolves a link group left with one clip, and undo brings all back")
    {
        test::ProjectWithClip p;
        auto* second = links::duplicate (*p.clip, 16.0, true);
        auto* third = links::duplicate (*p.clip, 32.0, true);
        REQUIRE (third != nullptr);
        p.project->clearUndoHistory();

        p.project->beginGesture ("Delete one");
        clips::remove ({ third });
        CHECK (links::getGroup (*p.clip).size() == 2);
        CHECK (links::isLinked (*second));

        p.project->beginGesture ("Delete another");
        clips::remove ({ second });
        CHECK_FALSE (links::isLinked (*p.clip));

        p.project->undo();
        p.project->undo();
        CHECK (clips::getMidiClips (*p.track).size() == 3);

        for (auto* clip : clips::getMidiClips (*p.track))
            CHECK (links::getGroup (*clip).size() == 3);
    }
}
