#include <doctest.h>

#include "TestSession.h"
#include "core/BassLines.h"
#include "core/Project.h"

using namespace harmonia;
using namespace harmonia::core;
namespace bass = bass_lines;

namespace
{
    /** C for a bar, A minor for a bar, F over A for a bar. */
    std::vector<chords::Span> threeChords()
    {
        return { { 0.0, 4.0, "C" }, { 4.0, 8.0, "Am" }, { 8.0, 12.0, "F/A" } };
    }

    std::vector<Note> generate (bass::Pattern pattern, const std::vector<chords::Span>& spans = threeChords())
    {
        bass::Settings s;
        s.pattern = pattern;
        return bass::generate (spans, s);
    }

    std::vector<int> pitchesOf (const std::vector<Note>& notes)
    {
        std::vector<int> pitches;

        for (const auto& n : notes)
            pitches.push_back (n.pitch);

        return pitches;
    }

    std::vector<double> startsOf (const std::vector<Note>& notes)
    {
        std::vector<double> starts;

        for (const auto& n : notes)
            starts.push_back (n.startBeat);

        return starts;
    }

    te::MidiClip& addPart (core::Project& project, te::AudioTrack& track, double start, double length,
                           std::initializer_list<int> pitches)
    {
        auto* clip = project.addMidiClip (track, start, length, track.getName());
        REQUIRE (clip != nullptr);

        for (int pitch : pitches)
            clip->getSequence().addNote (pitch, te::BeatPosition(), te::BeatDuration::fromBeats (length), 100, 0, nullptr);

        return *clip;
    }
}

TEST_SUITE ("core::bass_lines")
{
    TEST_CASE ("the root (or the bass note) of a chord symbol")
    {
        CHECK (bass::rootOf ("C") == 0);
        CHECK (bass::rootOf ("Am7") == 9);
        CHECK (bass::rootOf ("F#m") == 6);
        CHECK (bass::rootOf ("Bb") == 10);
        CHECK (bass::rootOf ("C/E") == 0);
        CHECK (bass::rootOf ("C/E", true) == 4);
        CHECK (bass::rootOf ("D7/F#", true) == 6);
        CHECK (bass::rootOf ("G", true) == 7);
        CHECK (bass::rootOf ("") == -1);
        CHECK (bass::rootOf ("x") == -1);
    }

    TEST_CASE ("held, on the beats, in eighths: the bass note of each chord")
    {
        const auto held = generate (bass::Pattern::held);
        CHECK (pitchesOf (held) == std::vector<int> { 36, 45, 45 });      // C2, A2, and A2 under F/A
        CHECK (startsOf (held) == std::vector<double> { 0.0, 4.0, 8.0 });
        CHECK (held[0].lengthBeats == doctest::Approx (3.6));
        CHECK (held[0].velocity == 100);

        bass::Settings roots;
        roots.pattern = bass::Pattern::held;
        roots.slashBass = false;
        roots.octave = 1;
        CHECK (pitchesOf (bass::generate (threeChords(), roots)) == std::vector<int> { 24, 33, 29 });     // F1 under F/A

        const auto beats = generate (bass::Pattern::beats);
        REQUIRE (beats.size() == 12);
        CHECK (startsOf (beats) == std::vector<double> { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
        CHECK (beats[5].pitch == 45);
        CHECK (beats[1].lengthBeats == doctest::Approx (0.9));

        const auto eighths = generate (bass::Pattern::eighths);
        REQUIRE (eighths.size() == 24);
        CHECK (eighths[1].startBeat == doctest::Approx (0.5));
        CHECK (eighths[0].velocity == 100);
        CHECK (eighths[1].velocity == 85);      // between the beats, softer
        CHECK (eighths[2].velocity == 100);
    }

    TEST_CASE ("root and fifth, octaves, walking")
    {
        CHECK (pitchesOf (generate (bass::Pattern::rootFifth))
               == std::vector<int> { 36, 43, 36, 43, 45, 52, 45, 52, 45, 48, 45, 48 });     // F/A: A, then F's fifth C

        const auto octaves = generate (bass::Pattern::octaves);
        REQUIRE (octaves.size() == 24);
        CHECK (pitchesOf (octaves)[0] == 36);
        CHECK (pitchesOf (octaves)[1] == 48);
        CHECK (pitchesOf (octaves)[8] == 45);
        CHECK (pitchesOf (octaves)[9] == 57);

        // Root, third, fifth, then a half step under the next chord's bass.
        CHECK (pitchesOf (generate (bass::Pattern::walking))
               == std::vector<int> { 36, 40, 43, 44,  45, 48, 52, 44,  45, 45, 48, 53 });
    }

    TEST_CASE ("short chords and chords off the beat")
    {
        const std::vector<chords::Span> spans { { 0.5, 2.0, "G" }, { 2.0, 2.5, "D" } };
        const auto beats = generate (bass::Pattern::beats, spans);
        CHECK (startsOf (beats) == std::vector<double> { 0.5, 1.5, 2.0 });
        CHECK (beats[0].velocity == 100);       // the chord change is the accent
        CHECK (beats[1].velocity == 85);
        CHECK (beats[2].lengthBeats == doctest::Approx (0.5));      // never past the chord's end

        // The walk leads on only into a chord right after it.
        const std::vector<chords::Span> gap { { 0.0, 4.0, "C" }, { 6.0, 8.0, "G" } };
        CHECK (pitchesOf (generate (bass::Pattern::walking, gap)) == std::vector<int> { 36, 40, 43, 48, 43, 47 });

        CHECK (generate (bass::Pattern::beats, { { 0.0, 4.0, "" } }).empty());
    }

    TEST_CASE ("the harmony comes from the other tracks: not drums, not muted, cut to the stretch asked")
    {
        auto project = Project::createEmpty (test::session());
        auto& bassTrack = *project->getTracks().getFirst();
        bassTrack.setName ("Bass");
        addPart (*project, bassTrack, 0.0, 8.0, { 30 });

        auto& keys = *project->addTrack ("Keys");
        addPart (*project, keys, 0.0, 4.0, { 60, 64, 67 });
        addPart (*project, keys, 4.0, 4.0, { 57, 60, 64 });

        addPart (*project, *project->addTrack ("Drums"), 0.0, 8.0, { 36, 38, 42 });

        auto& muted = *project->addTrack ("Pad");
        addPart (*project, muted, 0.0, 8.0, { 62, 65, 69 });
        muted.setMute (true);

        const auto harmony = bass::getHarmony (project->getEdit(), &bassTrack, 0.0, 8.0);
        CHECK (pitchesOf (harmony) == std::vector<int> { 60, 64, 67, 57, 60, 64 });
        CHECK (chords::findChords (harmony) == std::vector<chords::Span> { { 0.0, 4.0, "C" }, { 4.0, 8.0, "Am" } });

        // Cut to the stretch.
        const auto middle = bass::getHarmony (project->getEdit(), &bassTrack, 2.0, 6.0);
        REQUIRE (middle.size() == 6);
        CHECK (middle[0].startBeat == doctest::Approx (2.0));
        CHECK (middle[0].lengthBeats == doctest::Approx (2.0));
        CHECK (middle[5].getEndBeat() == doctest::Approx (6.0));

        // The bass track's own notes count when another track asks.
        CHECK (bass::getHarmony (project->getEdit(), &keys, 0.0, 8.0).size() == 1);
    }
}
