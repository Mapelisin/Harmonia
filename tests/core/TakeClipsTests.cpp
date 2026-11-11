#include <doctest.h>

#include "TestProject.h"
#include "TestSession.h"
#include "core/Clips.h"
#include "core/Controllers.h"
#include "core/NoteEditor.h"
#include "core/TakeClips.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace clips = core::clips;
namespace controllers = core::controllers;
using core::Note;
using core::TakeClips;

namespace
{
    Note makeNote (int pitch, double start, double length = 1.0)
    {
        Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }

    /** (song start, song end, pitch) of each note the clip plays, in time order. */
    struct Heard
    {
        double start = 0.0, end = 0.0;
        int pitch = 0;

        bool operator== (const Heard& other) const
        {
            return std::abs (start - other.start) < 1.0e-6 && std::abs (end - other.end) < 1.0e-6 && pitch == other.pitch;
        }
    };

    std::vector<Heard> heard (const te::MidiClip& clip)
    {
        std::vector<Heard> result;

        for (const auto& p : clips::getPlayedNotes (clip))
            result.push_back ({ p.startBeat, p.endBeat, p.note.pitch });

        std::sort (result.begin(), result.end(), [] (const Heard& a, const Heard& b) { return a.start < b.start; });
        return result;
    }

    using Range = std::pair<double, double>;

    Range rangeOf (const te::Clip& clip)
    {
        return { clips::getStartBeat (clip), clips::getEndBeat (clip) };
    }

    std::vector<Range> rangesOf (te::AudioTrack& track)
    {
        std::vector<Range> ranges;

        for (auto* clip : clips::getMidiClips (track))
            ranges.push_back (rangeOf (*clip));

        return ranges;
    }

    bool same (const std::vector<Range>& a, const std::vector<Range>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs (a[i].first - b[i].first) > 1.0e-6 || std::abs (a[i].second - b[i].second) > 1.0e-6)
                return false;

        return true;
    }

    /** A song with one instrument track and no clips; 4 beats to the bar. */
    struct EmptyTrack
    {
        EmptyTrack()
        {
            track = project->getTracks().getFirst();
            REQUIRE (track != nullptr);
            REQUIRE (clips::getMidiClips (*track).isEmpty());
            project->clearUndoHistory();
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* track = nullptr;
    };
}

TEST_SUITE ("core: the clips of a take")
{
    TEST_CASE ("what is played where the track has no clip makes one: from the bar of the first note, growing by bars")
    {
        EmptyTrack f;
        f.project->beginGesture ("Record notes");
        TakeClips take (*f.project, *f.track);
        CHECK (take.getMadeClips().isEmpty());

        auto* clip = take.addNote (makeNote (60, 5.5, 1.0));
        REQUIRE (clip != nullptr);
        CHECK (same (rangesOf (*f.track), { { 4.0, 8.0 } }));
        CHECK (heard (*clip) == std::vector<Heard> { { 5.5, 6.5, 60 } });
        CHECK (take.getMadeClips() == juce::Array<te::MidiClip*> { clip });
        CHECK (clip->getName() == f.track->getName());

        // Later in the same bars: nothing grows.
        CHECK (take.addNote (makeNote (62, 7.0, 0.5)) == clip);
        CHECK (same (rangesOf (*f.track), { { 4.0, 8.0 } }));

        // Begun inside it and held on past its end: it grows to hold the whole note.
        CHECK (take.addNote (makeNote (63, 7.5, 1.0)) == clip);
        CHECK (same (rangesOf (*f.track), { { 4.0, 12.0 } }));

        // Past its end: the same clip, to the end of the bar the note ends in.
        CHECK (take.addNote (makeNote (64, 9.0, 0.5)) == clip);
        CHECK (same (rangesOf (*f.track), { { 4.0, 12.0 } }));

        // A long pause, then a note over a bar line: still one clip for the take.
        CHECK (take.addNote (makeNote (65, 30.0, 3.0)) == clip);
        CHECK (same (rangesOf (*f.track), { { 4.0, 36.0 } }));

        // A note ending exactly on a bar line doesn't take the next bar.
        CHECK (take.addNote (makeNote (67, 38.0, 2.0)) == clip);
        CHECK (same (rangesOf (*f.track), { { 4.0, 40.0 } }));

        CHECK (heard (*clip) == std::vector<Heard> { { 5.5, 6.5, 60 }, { 7.0, 7.5, 62 }, { 7.5, 8.5, 63 }, { 9.0, 9.5, 64 },
                                                     { 30.0, 33.0, 65 }, { 38.0, 40.0, 67 } });
        CHECK (take.getMadeClips().size() == 1);

        // The whole take is one step back.
        f.project->getEdit().dispatchPendingUpdatesSynchronously();
        f.project->undo();
        CHECK (clips::getMidiClips (*f.track).isEmpty());
        CHECK (take.getMadeClips().isEmpty());
        CHECK_FALSE (f.project->canUndo());
    }

    TEST_CASE ("what is played inside a clip the track has goes into it; a new clip never grows into one that was there")
    {
        EmptyTrack f;
        auto* there = f.project->addMidiClip (*f.track, 16.0, 16.0, "Verse");
        REQUIRE (there != nullptr);
        core::NoteEditor (*there).add (makeNote (72, 1.0));           // song beat 17
        f.project->clearUndoHistory();

        f.project->beginGesture ("Record notes");
        TakeClips take (*f.project, *f.track);

        CHECK (take.addNote (makeNote (60, 18.0, 1.0)) == there);
        CHECK (take.getMadeClips().isEmpty());
        CHECK (heard (*there) == std::vector<Heard> { { 17.0, 18.0, 72 }, { 18.0, 19.0, 60 } });

        // The same part again (the loop's next time round) doesn't stack copies; another pitch does go in.
        CHECK (take.addNote (makeNote (60, 18.0, 0.5)) == there);
        CHECK (take.addNote (makeNote (72, 17.0, 1.0)) == there);
        CHECK (heard (*there).size() == 2);
        CHECK (take.addNote (makeNote (64, 18.0, 1.0)) == there);
        CHECK (heard (*there).size() == 3);

        // A note held past the clip's end stops there.
        CHECK (take.addNote (makeNote (65, 31.0, 4.0)) == there);
        CHECK (heard (*there).back() == Heard { 31.0, 32.0, 65 });
        CHECK (same (rangesOf (*f.track), { { 16.0, 32.0 } }));

        // Before it: a clip of the take's own.
        auto* before = take.addNote (makeNote (67, 1.0, 1.0));
        REQUIRE (before != nullptr);
        CHECK (before != there);
        CHECK (same (rangesOf (*f.track), { { 0.0, 4.0 }, { 16.0, 32.0 } }));

        // It grows up to the clip that was there and no further: a note held on into that one
        // stops where it begins.
        CHECK (take.addNote (makeNote (70, 14.5, 4.0)) == before);
        CHECK (same (rangesOf (*f.track), { { 0.0, 16.0 }, { 16.0, 32.0 } }));
        CHECK (heard (*before) == std::vector<Heard> { { 1.0, 2.0, 67 }, { 14.5, 16.0, 70 } });

        // After it: another one (the take's first can't reach over the clip between them).
        auto* after = take.addNote (makeNote (69, 33.0, 1.0));
        REQUIRE (after != nullptr);
        CHECK (after != there);
        CHECK (after != before);
        CHECK (same (rangesOf (*f.track), { { 0.0, 16.0 }, { 16.0, 32.0 }, { 32.0, 36.0 } }));
        CHECK (take.getMadeClips() == juce::Array<te::MidiClip*> { before, after });

        // Made right against the clip that was there, when the first note is in the bar before it.
        EmptyTrack g;
        auto* chorus = g.project->addMidiClip (*g.track, 16.0, 16.0, "Chorus");
        REQUIRE (chorus != nullptr);
        g.project->beginGesture ("Record notes");
        TakeClips other (*g.project, *g.track);
        auto* pickup = other.addNote (makeNote (67, 14.5, 4.0));
        REQUIRE (pickup != nullptr);
        CHECK (same (rangesOf (*g.track), { { 12.0, 16.0 }, { 16.0, 32.0 } }));
        CHECK (heard (*pickup) == std::vector<Heard> { { 14.5, 16.0, 67 } });
    }

    TEST_CASE ("played before the take's clip (the song went round a loop): the clip gets room at its beginning")
    {
        EmptyTrack f;
        f.project->beginGesture ("Record notes");
        TakeClips take (*f.project, *f.track);

        auto* clip = take.addNote (makeNote (60, 9.0, 1.0));
        REQUIRE (clip != nullptr);
        CHECK (take.addController (controllers::Controller::modWheel, 10.0, 0.75f) == clip);
        CHECK (same (rangesOf (*f.track), { { 8.0, 12.0 } }));

        // The loop's next time round, two bars earlier: the same clip, and what it had stays
        // where it was in the song.
        CHECK (take.addNote (makeNote (64, 2.25, 0.5)) == clip);
        CHECK (same (rangesOf (*f.track), { { 0.0, 12.0 } }));
        CHECK (heard (*clip) == std::vector<Heard> { { 2.25, 2.75, 64 }, { 9.0, 10.0, 60 } });

        const auto wheel = controllers::getPlayed (*clip, controllers::Controller::modWheel);
        REQUIRE (wheel.size() == 1);
        CHECK (wheel[0].beat == doctest::Approx (10.0));
        CHECK (wheel[0].value == doctest::Approx (0.75f).epsilon (0.01));
        CHECK (take.getMadeClips().size() == 1);

        // A key held from before the clip's first bar, let go after a later note was written.
        EmptyTrack g;
        auto* second = g.project->addTrack ("Second");
        REQUIRE (second != nullptr);
        auto* early = g.project->addMidiClip (*second, 0.0, 6.0, "Ends inside a bar");
        REQUIRE (early != nullptr);
        g.project->beginGesture ("Record notes");
        TakeClips other (*g.project, *second);

        auto* made = other.addNote (makeNote (60, 9.0, 1.0));
        REQUIRE (made != nullptr);
        CHECK (other.addNote (makeNote (55, 6.5, 4.0)) == made);

        // Not into the clip that ends at beat 6.
        CHECK (same (rangesOf (*second), { { 0.0, 6.0 }, { 6.0, 12.0 } }));
        CHECK (heard (*made) == std::vector<Heard> { { 6.5, 10.5, 55 }, { 9.0, 10.0, 60 } });

        // A bass note held under short ones, from the bar before the clip to the bar after it:
        // the clip grows both ways and the note is whole.
        EmptyTrack h;
        h.project->beginGesture ("Record notes");
        TakeClips third (*h.project, *h.track);
        auto* short1 = third.addNote (makeNote (72, 9.0, 0.5));
        REQUIRE (short1 != nullptr);
        CHECK (same (rangesOf (*h.track), { { 8.0, 12.0 } }));
        CHECK (third.addNote (makeNote (36, 6.5, 7.0)) == short1);
        CHECK (same (rangesOf (*h.track), { { 4.0, 16.0 } }));
        CHECK (heard (*short1) == std::vector<Heard> { { 6.5, 13.5, 36 }, { 9.0, 9.5, 72 } });
        CHECK (third.getMadeClips().size() == 1);
    }

    TEST_CASE ("a looping clip takes what is played in any of its times round into its loop")
    {
        EmptyTrack f;
        auto* looped = f.project->addMidiClip (*f.track, 8.0, 4.0, "Loop");
        REQUIRE (looped != nullptr);
        clips::setLooping (*looped, true);
        clips::setEnd (*looped, 20.0, 0.25);                // three times round: 8..12, 12..16, 16..20
        f.project->clearUndoHistory();

        f.project->beginGesture ("Record notes");
        TakeClips take (*f.project, *f.track);

        // Played in the third time round, a beat and a half into it.
        CHECK (take.addNote (makeNote (60, 17.5, 1.0)) == looped);
        CHECK (take.getMadeClips().isEmpty());

        const auto notes = test::readNotes (*looped);
        REQUIRE (notes.size() == 1);
        CHECK (notes[0].startBeat == doctest::Approx (1.5));

        // It sounds in every time round.
        CHECK (heard (*looped) == std::vector<Heard> { { 9.5, 10.5, 60 }, { 13.5, 14.5, 60 }, { 17.5, 18.5, 60 } });

        // Held over the loop's end: it stops there.
        CHECK (take.addNote (makeNote (62, 15.0, 3.0)) == looped);
        const auto all = test::readNotes (*looped);
        REQUIRE (all.size() == 2);
        CHECK (all[1].startBeat == doctest::Approx (3.0));
        CHECK (all[1].lengthBeats == doctest::Approx (1.0));
    }

    TEST_CASE ("a wheel moved where the track has no clip makes one too; nothing goes onto a track that can't take notes")
    {
        EmptyTrack f;
        f.project->beginGesture ("Record notes");
        TakeClips take (*f.project, *f.track);
        CHECK (TakeClips::canRecordOn (*f.track));

        auto* clip = take.addController (controllers::Controller::pitchBend, 21.0, 0.9f);
        REQUIRE (clip != nullptr);
        CHECK (same (rangesOf (*f.track), { { 20.0, 24.0 } }));
        const auto bend = controllers::getPlayed (*clip, controllers::Controller::pitchBend);
        REQUIRE (bend.size() == 1);
        CHECK (bend[0].beat == doctest::Approx (21.0));

        // An audio track: nowhere.
        auto* audio = f.project->addTrack ("Guitar");
        REQUIRE (audio != nullptr);
        core::tracks::setKind (*audio, core::tracks::Kind::audio);
        CHECK_FALSE (TakeClips::canRecordOn (*audio));

        TakeClips onAudio (*f.project, *audio);
        CHECK (onAudio.addNote (makeNote (60, 1.0)) == nullptr);
        CHECK (onAudio.addController (controllers::Controller::modWheel, 1.0, 0.5f) == nullptr);
        CHECK (audio->getClips().isEmpty());
        CHECK (onAudio.getMadeClips().isEmpty());
    }
}
