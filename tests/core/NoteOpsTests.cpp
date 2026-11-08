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

TEST_SUITE ("core::note_ops: commands")
{
    TEST_CASE ("legato stretches each note to the next start; a chord all reaches it; the last keep their length")
    {
        const auto out = note_ops::legato ({ makeNote (60, 0.0, 0.25), makeNote (64, 0.0, 2.0),
                                             makeNote (62, 1.0, 0.5), makeNote (65, 3.0, 0.5) });
        CHECK (out[0].lengthBeats == doctest::Approx (1.0));
        CHECK (out[1].lengthBeats == doctest::Approx (1.0));     // the overlap is cut too
        CHECK (out[2].lengthBeats == doctest::Approx (2.0));
        CHECK (out[3].lengthBeats == doctest::Approx (0.5));
        CHECK (out[3].startBeat == doctest::Approx (3.0));
    }

    TEST_CASE ("chop cuts a long note into step pieces with the same values")
    {
        auto n = makeNote (60, 1.0, 1.1);
        n.velocity = 77;
        const auto pieces = note_ops::chop (n, 0.25);
        REQUIRE (pieces.size() == 5);

        for (size_t i = 0; i < 4; ++i)
        {
            CHECK (pieces[i].startBeat == doctest::Approx (1.0 + 0.25 * (double) i));
            CHECK (pieces[i].lengthBeats == doctest::Approx (0.25));
        }

        CHECK (pieces[4].lengthBeats == doctest::Approx (0.1));

        for (const auto& p : pieces)
            CHECK (p.velocity == 77);

        CHECK (note_ops::chop (makeNote (60, 0.0, 0.25), 0.25).size() == 1);
        CHECK (note_ops::chop (n, 0.0).size() == 1);
    }

    TEST_CASE ("glue joins touching and overlapping notes of the same pitch and colour")
    {
        std::vector<Note> notes { makeNote (60, 1.0, 1.0), makeNote (60, 0.0, 1.0), makeNote (60, 1.5, 1.0),
                                  makeNote (60, 3.0, 1.0), makeNote (62, 1.0, 1.0), makeNote (60, 2.5, 1.0) };
        notes[1].velocity = 90;
        notes[5].colour = 3;

        const auto glued = note_ops::glue (notes);
        CHECK (glued.removed == std::vector<size_t> { 0, 2 });
        CHECK (glued.notes[1].startBeat == doctest::Approx (0.0));
        CHECK (glued.notes[1].lengthBeats == doctest::Approx (2.5));
        CHECK (glued.notes[1].velocity == 90);
        CHECK (glued.notes[3] == notes[3]);     // half a beat away
        CHECK (glued.notes[4] == notes[4]);     // another pitch
        CHECK (glued.notes[5] == notes[5]);     // another colour
    }

    TEST_CASE ("rotate wraps the notes around inside the whole bars they start in")
    {
        const std::vector<Note> notes { makeNote (60, 4.0, 0.25), makeNote (62, 7.75, 0.5) };
        const auto bars = note_ops::getWholeBars (notes, 4.0);
        CHECK (bars.start == doctest::Approx (4.0));
        CHECK (bars.end == doctest::Approx (8.0));

        const auto right = note_ops::rotate (notes, bars, 0.25);
        CHECK (right[0].startBeat == doctest::Approx (4.25));
        CHECK (right[1].startBeat == doctest::Approx (4.0));
        CHECK (right[1].lengthBeats == doctest::Approx (0.5));

        const auto left = note_ops::rotate (notes, bars, -0.25);
        CHECK (left[0].startBeat == doctest::Approx (7.75));
        CHECK (left[1].startBeat == doctest::Approx (7.5));

        CHECK (note_ops::rotate (notes, bars, 4.0)[1].startBeat == doctest::Approx (7.75));
        CHECK (note_ops::rotate (notes, bars, -12.0)[0].startBeat == doctest::Approx (4.0));
    }

    TEST_CASE ("insert space moves later notes; delete space cuts the time out")
    {
        const std::vector<Note> notes { makeNote (60, 0.0, 1.0), makeNote (62, 1.0, 2.0), makeNote (64, 2.0, 0.5),
                                        makeNote (65, 4.0, 1.0), makeNote (67, 0.5, 3.0) };
        const note_ops::BeatRange range { 2.0, 3.0 };

        const auto inserted = note_ops::insertSpace (notes, range);
        CHECK (inserted[0] == notes[0]);
        CHECK (inserted[1] == notes[1]);      // starts before: stays
        CHECK (inserted[2].startBeat == doctest::Approx (3.0));
        CHECK (inserted[3].startBeat == doctest::Approx (5.0));
        CHECK (inserted[4] == notes[4]);

        const auto deleted = note_ops::deleteSpace (notes, range);
        CHECK (deleted.removed == std::vector<size_t> { 2 });
        CHECK (deleted.notes[0] == notes[0]);
        CHECK (deleted.notes[1].lengthBeats == doctest::Approx (1.0));   // ended inside: cut at the start
        CHECK (deleted.notes[3].startBeat == doctest::Approx (3.0));
        CHECK (deleted.notes[4].startBeat == doctest::Approx (0.5));
        CHECK (deleted.notes[4].lengthBeats == doctest::Approx (2.0));   // across it: lost one beat
    }
}
