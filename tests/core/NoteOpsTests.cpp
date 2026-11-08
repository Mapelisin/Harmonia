#include <doctest.h>

#include "TestProject.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/NoteOps.h"

using namespace harmonia;
using core::Note;
using core::NoteProperty;
namespace note_ops = core::note_ops;
namespace property = core::note_property;

namespace
{
    Note makeNote (int pitch, double start, double length)
    {
        Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }
}

TEST_SUITE ("core::note_ops")
{
    TEST_CASE ("quantize snaps starts, and ends unless only starts are asked for")
    {
        const auto n = makeNote (60, 1.1, 0.9);

        const auto both = note_ops::quantized (n, 0.25, false);
        CHECK (both.startBeat == doctest::Approx (1.0));
        CHECK (both.getEndBeat() == doctest::Approx (2.0));

        const auto starts = note_ops::quantized (n, 0.25, true);
        CHECK (starts.startBeat == doctest::Approx (1.0));
        CHECK (starts.lengthBeats == doctest::Approx (0.9));

        // A note shorter than the grid keeps one grid step.
        const auto tiny = note_ops::quantized (makeNote (60, 2.0, 0.05), 0.25, false);
        CHECK (tiny.lengthBeats == doctest::Approx (0.25));
    }

    TEST_CASE ("split keeps every property and covers the original")
    {
        auto n = test::makeRichNote (64, 1.0);
        n.lengthBeats = 2.0;

        const auto [left, right] = note_ops::split (n, 1.5);
        CHECK (left.startBeat == doctest::Approx (1.0));
        CHECK (left.lengthBeats == doctest::Approx (0.5));
        CHECK (right.startBeat == doctest::Approx (1.5));
        CHECK (right.lengthBeats == doctest::Approx (1.5));
        CHECK (right.velocity == n.velocity);
        CHECK (right.pan == n.pan);
        CHECK (right.colour == n.colour);
    }

    TEST_CASE ("transposition and moves are limited so the chord keeps its shape")
    {
        const std::vector<Note> chord { makeNote (120, 1.0, 1.0), makeNote (124, 1.0, 1.0), makeNote (127 - 1, 2.0, 1.0) };

        CHECK (note_ops::limitTransposition (chord, 5) == 1);
        CHECK (note_ops::limitTransposition (chord, -200) == -120);
        CHECK (note_ops::limitMove (chord, -3.0) == doctest::Approx (-1.0));
        CHECK (note_ops::limitMove (chord, 2.0) == doctest::Approx (2.0));
    }

    TEST_CASE ("duplicate moves by the span rounded up to whole beats")
    {
        CHECK (note_ops::getDuplicateOffset ({ makeNote (60, 0.0, 0.5), makeNote (62, 1.0, 0.75) }) == doctest::Approx (2.0));
        CHECK (note_ops::getDuplicateOffset ({ makeNote (60, 4.0, 4.0) }) == doctest::Approx (4.0));
        CHECK (note_ops::getDuplicateOffset ({ makeNote (60, 2.5, 0.25) }) == doctest::Approx (1.0));
    }

    TEST_CASE ("per-note values convert to the 0..1 panel scale and back")
    {
        Note n;

        for (auto p : property::getAll())
        {
            CHECK (property::getName (p).isNotEmpty());
            CHECK (property::get (n, p) == doctest::Approx (property::getDefault (p)));

            property::set (n, p, 1.0f);
            CHECK (property::get (n, p) == doctest::Approx (1.0f));
            property::set (n, p, 0.0f);
            CHECK (property::get (n, p) == doctest::Approx (0.0f));
            property::set (n, p, 2.0f);   // clamped
            CHECK (property::get (n, p) == doctest::Approx (1.0f));
        }

        CHECK (n.velocity == 127);
        CHECK (n.pan == doctest::Approx (1.0f));
        CHECK (n.finePitchCents == doctest::Approx (core::note::maxFinePitchCents));

        property::set (n, NoteProperty::pan, 0.375f);
        CHECK (property::describe (n, NoteProperty::pan) == "Pan 25% L");
        CHECK (property::isBipolar (NoteProperty::pan));
        CHECK_FALSE (property::isBipolar (NoteProperty::velocity));
    }
}

TEST_SUITE ("core::clips")
{
    TEST_CASE ("clips grow by whole bars to fit notes, with their linked copies, and undo shrinks them")
    {
        test::ProjectWithClip p;
        auto* copy = core::clip_links::duplicate (*p.clip, 32.0, true);
        REQUIRE (copy != nullptr);

        CHECK (core::clips::getBeatsPerBar (p.project->getEdit()) == doctest::Approx (4.0));
        CHECK (core::clips::getEndBeat (*p.clip) == doctest::Approx (16.0));

        p.project->beginGesture ("grow");
        core::clips::extendToCover (*p.clip, 17.25);
        CHECK (core::clips::getEndBeat (*p.clip) == doctest::Approx (20.0));
        CHECK (core::clips::getEndBeat (*copy) == doctest::Approx (52.0));

        // Never shrinks.
        core::clips::extendToCover (*p.clip, 3.0);
        CHECK (core::clips::getEndBeat (*p.clip) == doctest::Approx (20.0));

        p.project->undo();
        CHECK (core::clips::getEndBeat (*p.clip) == doctest::Approx (16.0));
        CHECK (core::clips::getEndBeat (*copy) == doctest::Approx (48.0));
    }

    TEST_CASE ("a track's main clip is found or created")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();

        CHECK (core::clips::getMainClip (track) == nullptr);
        auto* clip = core::clips::getOrAddMainClip (*project, track, 8.0);
        REQUIRE (clip != nullptr);
        CHECK (core::clips::getOrAddMainClip (*project, track, 8.0) == clip);
        CHECK (core::clips::getStartBeat (*clip) == doctest::Approx (0.0));
        CHECK (core::clips::getEndBeat (*clip) == doctest::Approx (8.0));
        CHECK (core::clips::getContentStartBeat (*clip) == doctest::Approx (0.0));
    }
}
