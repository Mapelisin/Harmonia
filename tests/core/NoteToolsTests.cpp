#include <doctest.h>

#include "core/NoteTools.h"

using namespace harmonia;
using core::Note;
namespace tools = core::note_tools;

namespace
{
    Note makeNote (int pitch, double start, double length, int velocity = 100)
    {
        Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        n.velocity = velocity;
        return n;
    }

    /** C major (C E G, given out of order) at beat 0 and F major at beat 2, one beat long. */
    std::vector<Note> twoChords()
    {
        return { makeNote (64, 0.0, 1.0), makeNote (60, 0.0, 1.0), makeNote (67, 0.0, 1.0),
                 makeNote (65, 2.0, 1.0), makeNote (69, 2.0, 1.0), makeNote (72, 2.0, 1.0) };
    }
}

TEST_SUITE ("core::note_tools")
{
    TEST_CASE ("chords are the notes starting together, from the lowest up")
    {
        auto notes = twoChords();
        notes.push_back (makeNote (50, 1.0, 0.5));

        const auto chords = tools::findChords (notes);
        REQUIRE (chords.size() == 3);
        CHECK (chords[0] == std::vector<size_t> { 1, 0, 2 });
        CHECK (chords[1] == std::vector<size_t> { 6 });
        CHECK (chords[2] == std::vector<size_t> { 3, 4, 5 });
    }

    TEST_CASE ("strum spreads each chord: up, down or alternating, ends kept or not")
    {
        tools::StrumSettings s;
        s.timeBeats = 0.3;

        auto out = tools::strum (twoChords(), s);
        CHECK (out[1].startBeat == doctest::Approx (0.0));     // C
        CHECK (out[0].startBeat == doctest::Approx (0.15));    // E
        CHECK (out[2].startBeat == doctest::Approx (0.3));     // G
        CHECK (out[2].getEndBeat() == doctest::Approx (1.0));  // ends stay
        CHECK (out[5].startBeat == doctest::Approx (2.3));     // the top of the second chord

        s.direction = tools::StrumDirection::down;
        out = tools::strum (twoChords(), s);
        CHECK (out[2].startBeat == doctest::Approx (0.0));
        CHECK (out[1].startBeat == doctest::Approx (0.3));

        s.direction = tools::StrumDirection::alternate;
        s.keepEnds = false;
        out = tools::strum (twoChords(), s);
        CHECK (out[1].startBeat == doctest::Approx (0.0));     // first chord up
        CHECK (out[5].startBeat == doctest::Approx (2.0));     // second chord down
        CHECK (out[3].startBeat == doctest::Approx (2.3));
        CHECK (out[3].lengthBeats == doctest::Approx (1.0));   // moved, not shortened

        // Tension: the notes come in faster at first.
        s.direction = tools::StrumDirection::up;
        s.tension = 1.0;
        out = tools::strum (twoChords(), s);
        CHECK (out[0].startBeat < 0.1);
        CHECK (out[2].startBeat == doctest::Approx (0.3));

        // Velocity: the last note louder (up to 127).
        s = {};
        s.velocity = 0.5;
        out = tools::strum (twoChords(), s);
        CHECK (out[1].velocity == 100);
        CHECK (out[0].velocity == 125);
        CHECK (out[2].velocity == 127);

        // A single note is left alone.
        CHECK (tools::strum ({ makeNote (60, 1.0, 1.0) }, s)[0] == makeNote (60, 1.0, 1.0));
    }

    TEST_CASE ("humanize changes within the amounts, the same way for the same variation")
    {
        std::vector<Note> notes;

        for (int i = 0; i < 32; ++i)
            notes.push_back (makeNote (60, 1.0 + i * 0.25, 0.25));

        tools::HumanizeSettings s;
        CHECK (tools::humanize (notes, s) == notes);

        s.timingBeats = 0.02;
        s.velocity = 10.0;
        s.length = 0.2;
        const auto a = tools::humanize (notes, s);
        CHECK (a == tools::humanize (notes, s));
        CHECK_FALSE (a == notes);

        bool anyMoved = false;

        for (size_t i = 0; i < notes.size(); ++i)
        {
            CHECK (std::abs (a[i].startBeat - notes[i].startBeat) <= 0.021);
            CHECK (std::abs (a[i].velocity - notes[i].velocity) <= 10);
            CHECK (a[i].lengthBeats >= 0.199);
            CHECK (a[i].lengthBeats <= 0.301);
            anyMoved = anyMoved || a[i].startBeat != notes[i].startBeat;
        }

        CHECK (anyMoved);

        s.seed = 2;
        CHECK_FALSE (tools::humanize (notes, s) == a);

        // Never before the start of the clip.
        s.timingBeats = 5.0;
        for (const auto& n : tools::humanize (notes, s, 1.0))
            CHECK (n.startBeat >= 1.0);
    }

    TEST_CASE ("scale levels multiplies and offsets a value, around the middle for pan")
    {
        auto notes = std::vector<Note> { makeNote (60, 0.0, 1.0, 100), makeNote (62, 1.0, 1.0, 40) };
        notes[0].pan = 0.8f;
        notes[1].pan = -0.4f;

        tools::ScaleLevelsSettings s;
        CHECK (tools::scaleLevels (notes, s) == notes);

        s.multiply = 0.5;
        auto out = tools::scaleLevels (notes, s);
        CHECK (out[0].velocity == 50);
        CHECK (out[1].velocity == 20);

        s.multiply = 1.0;
        s.offset = 0.25;
        out = tools::scaleLevels (notes, s);
        CHECK (out[0].velocity == 127);
        CHECK (out[1].velocity == 72);

        s.property = core::NoteProperty::pan;
        s.multiply = 0.5;
        s.offset = 0.0;
        out = tools::scaleLevels (notes, s);
        CHECK (out[0].pan == doctest::Approx (0.4));
        CHECK (out[1].pan == doctest::Approx (-0.2));
        CHECK (out[0].velocity == 100);
    }

    TEST_CASE ("arpeggiate plays each chord note by note, for as long as the chord")
    {
        auto notes = twoChords();
        notes[1].velocity = 70;     // the C
        notes.push_back (makeNote (50, 1.0, 0.5));

        tools::ArpSettings s;
        s.stepBeats = 0.25;
        s.gate = 1.0;

        auto out = tools::arpeggiate (notes, s);
        CHECK (out.removed == std::vector<size_t> { 0, 1, 2, 3, 4, 5 });
        REQUIRE (out.added.size() == 8);

        std::vector<int> pitches;

        for (const auto& n : out.added)
            pitches.push_back (n.pitch);

        CHECK (pitches == std::vector<int> { 60, 64, 67, 60, 65, 69, 72, 65 });
        CHECK (out.added[3].startBeat == doctest::Approx (0.75));
        CHECK (out.added[3].lengthBeats == doctest::Approx (0.25));
        CHECK (out.added[0].velocity == 70);    // from the C
        CHECK (out.added[1].velocity == 100);
        CHECK (out.added[4].startBeat == doctest::Approx (2.0));

        s.pattern = tools::ArpPattern::down;
        s.gate = 0.5;
        out = tools::arpeggiate (notes, s);
        CHECK (out.added[0].pitch == 67);
        CHECK (out.added[0].lengthBeats == doctest::Approx (0.125));

        // Two octaves, up and down: the turning notes once.
        s.pattern = tools::ArpPattern::upDown;
        s.octaves = 2;
        s.stepBeats = 0.125;
        out = tools::arpeggiate ({ makeNote (60, 0.0, 2.0), makeNote (64, 0.0, 2.0), makeNote (67, 0.0, 2.0) }, s);
        pitches.clear();

        for (const auto& n : out.added)
            pitches.push_back (n.pitch);

        CHECK (pitches == std::vector<int> { 60, 64, 67, 72, 76, 79, 76, 72, 67, 64,
                                             60, 64, 67, 72, 76, 79 });
    }
}
