#include <doctest.h>

#include "core/NoteRecorder.h"

using namespace harmonia;
using core::InputQuantize;
using core::Note;
using core::NoteRecorder;

TEST_SUITE ("core::NoteRecorder")
{
    TEST_CASE ("a key held from one beat to another becomes a note with that start, length and velocity")
    {
        NoteRecorder r;
        r.start ({ 0.0, 16.0 });

        CHECK_FALSE (r.noteOn (60, 90, 1.25).has_value());
        CHECK_FALSE (r.noteOn (64, 70, 1.27).has_value());   // a chord

        const auto first = r.noteOff (60, 2.0);
        REQUIRE (first.has_value());
        CHECK (first->pitch == 60);
        CHECK (first->velocity == 90);
        CHECK (first->startBeat == doctest::Approx (1.25));
        CHECK (first->lengthBeats == doctest::Approx (0.75));

        // Still sounding: shown with its length so far.
        const auto sounding = r.getSoundingNotes (2.5);
        REQUIRE (sounding.size() == 1);
        CHECK (sounding[0].pitch == 64);
        CHECK (sounding[0].lengthBeats == doctest::Approx (1.23));

        // A key-up without a key-down (pressed before recording started) is ignored.
        CHECK_FALSE (r.noteOff (67, 2.5).has_value());
    }

    TEST_CASE ("a note held across the loop end stops at the loop end")
    {
        NoteRecorder r;
        r.start ({ 0.0, 8.0 });
        r.noteOn (60, 100, 7.5);

        // The playhead wrapped: the key comes up "before" it went down.
        const auto n = r.noteOff (60, 0.25);
        REQUIRE (n.has_value());
        CHECK (n->startBeat == doctest::Approx (7.5));
        CHECK (n->getEndBeat() == doctest::Approx (8.0));
    }

    TEST_CASE ("stopping finishes the notes still held; a very short tap still makes a note")
    {
        NoteRecorder r;
        r.start ({ 0.0, 16.0 });
        r.noteOn (60, 100, 3.0);
        r.noteOn (62, 100, 3.5);
        r.noteOff (62, 3.5);   // zero length

        const auto finished = r.stop (4.0);
        REQUIRE (finished.size() == 1);
        CHECK (finished[0].lengthBeats == doctest::Approx (1.0));
        CHECK_FALSE (r.isActive());

        // Nothing is recorded after the take.
        CHECK_FALSE (r.noteOn (60, 100, 5.0).has_value());
        CHECK (r.getSoundingNotes (5.0).empty());
    }

    TEST_CASE ("pressing a key that is still sounding finishes its note first")
    {
        NoteRecorder r;
        r.start ({ 0.0, 16.0 });
        r.noteOn (60, 100, 1.0);

        const auto previous = r.noteOn (60, 80, 2.0);
        REQUIRE (previous.has_value());
        CHECK (previous->lengthBeats == doctest::Approx (1.0));
        CHECK (r.getSoundingNotes (2.5).at (0).velocity == 80);
    }

    TEST_CASE ("the sustain pedal holds released notes until it comes up")
    {
        NoteRecorder r;
        r.start ({ 0.0, 16.0 });
        r.setSustain (true, 0.5);
        r.noteOn (60, 100, 1.0);
        r.noteOn (64, 100, 1.0);
        CHECK_FALSE (r.noteOff (60, 1.5).has_value());

        const auto finished = r.setSustain (false, 3.0);
        REQUIRE (finished.size() == 1);
        CHECK (finished[0].pitch == 60);
        CHECK (finished[0].getEndBeat() == doctest::Approx (3.0));

        // The key still held keeps sounding after the pedal.
        CHECK (r.getSoundingNotes (3.5).size() == 1);
        CHECK (r.noteOff (64, 4.0)->getEndBeat() == doctest::Approx (4.0));
    }

    TEST_CASE ("recorded notes are quantized as asked, a slightly early first beat goes to the loop start")
    {
        Note n;
        n.startBeat = 1.1;
        n.lengthBeats = 0.8;
        const core::note_ops::BeatRange loop { 0.0, 8.0 };

        const auto asPlayed = core::quantizeRecorded (n, InputQuantize::off, 0.25, loop);
        CHECK (asPlayed.startBeat == doctest::Approx (1.1).epsilon (0.001));
        CHECK (asPlayed.lengthBeats == doctest::Approx (0.8).epsilon (0.001));

        const auto starts = core::quantizeRecorded (n, InputQuantize::starts, 0.25, loop);
        CHECK (starts.startBeat == doctest::Approx (1.0));
        CHECK (starts.lengthBeats == doctest::Approx (0.8).epsilon (0.001));

        const auto both = core::quantizeRecorded (n, InputQuantize::startsAndEnds, 0.25, loop);
        CHECK (both.startBeat == doctest::Approx (1.0));
        CHECK (both.getEndBeat() == doctest::Approx (2.0));

        Note early;
        early.startBeat = 7.95;
        early.lengthBeats = 0.05;
        CHECK (core::quantizeRecorded (early, InputQuantize::starts, 0.25, loop).startBeat == doctest::Approx (0.0));
    }
}
