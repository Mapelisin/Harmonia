#include <doctest.h>

#include "TestProject.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/MidiFiles.h"
#include "core/NoteEditor.h"

using namespace harmonia;
namespace clips = core::clips;
using core::Note;

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

    /** (song start, pitch) of each note as the clip plays it, in time order. */
    std::vector<std::pair<double, int>> played (const te::MidiClip& clip)
    {
        std::vector<std::pair<double, int>> result;

        for (const auto& p : clips::getPlayedNotes (clip))
            result.emplace_back (p.startBeat, p.note.pitch);

        std::sort (result.begin(), result.end());
        return result;
    }

    using Played = std::vector<std::pair<double, int>>;

    /** The test clip (4 bars at the song's start), cut down to its first bar, with notes at
        beats 0 and 2 and one at 4 that the clip hides. */
    struct OneBar : test::ProjectWithClip
    {
        OneBar()
        {
            core::NoteEditor (*clip).add ({ makeNote (60, 0.0), makeNote (64, 2.0), makeNote (67, 4.0) });
            clips::setEnd (*clip, 4.0, 0.25);
            project->clearUndoHistory();
        }

        void step (const juce::String& name)      { project->beginGesture (name); }
    };
}

TEST_SUITE ("core::clips: looping")
{
    TEST_CASE ("a looping clip repeats its loop for as long as it is; off, the content plays once")
    {
        OneBar f;
        CHECK_FALSE (clips::isLooping (*f.clip));
        CHECK (played (*f.clip) == Played { { 0.0, 60 }, { 2.0, 64 } });

        f.step ("Loop");
        clips::setLooping (*f.clip, true);
        CHECK (clips::isLooping (*f.clip));
        CHECK (clips::getLoopRange (*f.clip) == juce::Range<double> (0.0, 4.0));
        CHECK (clips::getLoopSongRange (*f.clip) == juce::Range<double> (0.0, 4.0));
        CHECK (clips::getContentStartBeat (*f.clip) == doctest::Approx (0.0));
        CHECK (played (*f.clip) == Played { { 0.0, 60 }, { 2.0, 64 } });

        f.step ("Longer");
        clips::setEnd (*f.clip, 16.0, 0.25);
        CHECK (played (*f.clip) == Played { { 0.0, 60 }, { 2.0, 64 }, { 4.0, 60 }, { 6.0, 64 },
                                            { 8.0, 60 }, { 10.0, 64 }, { 12.0, 60 }, { 14.0, 64 } });

        const auto all = clips::getPlayedNotes (*f.clip);
        CHECK (all.back().repeat == 3);

        // Off: the clip keeps its length and plays its content once, the note at 4 too.
        f.step ("Stop looping");
        clips::setLooping (*f.clip, false);
        CHECK_FALSE (clips::isLooping (*f.clip));
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (16.0));
        CHECK (played (*f.clip) == Played { { 0.0, 60 }, { 2.0, 64 }, { 4.0, 67 } });

        f.project->undo();
        CHECK (clips::isLooping (*f.clip));
        CHECK (played (*f.clip).size() == 8);
        f.project->undo();
        f.project->undo();
        CHECK_FALSE (clips::isLooping (*f.clip));
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (4.0));
    }

    TEST_CASE ("a loop from the middle of the content; trimming the start or splitting keeps every note in place")
    {
        test::ProjectWithClip p;
        core::NoteEditor (*p.clip).add ({ makeNote (60, 0.0), makeNote (62, 4.0), makeNote (64, 8.0) });

        // The clip shows content beats 4..8 at song beats 4..8, then loops them.
        clips::setStart (*p.clip, 4.0, 0.25);
        clips::setEnd (*p.clip, 8.0, 0.25);
        clips::setLooping (*p.clip, true);
        CHECK (clips::getLoopRange (*p.clip) == juce::Range<double> (4.0, 8.0));
        CHECK (clips::getLoopSongRange (*p.clip) == juce::Range<double> (4.0, 8.0));
        clips::setEnd (*p.clip, 16.0, 0.25);
        CHECK (played (*p.clip) == Played { { 4.0, 62 }, { 8.0, 62 }, { 12.0, 62 } });

        // The start moved earlier: the loop goes on before (the note at 2.. would be at 0 — hidden: none there).
        clips::setStart (*p.clip, 2.0, 0.25);
        CHECK (clips::getStartBeat (*p.clip) == doctest::Approx (2.0));
        CHECK (played (*p.clip) == Played { { 4.0, 62 }, { 8.0, 62 }, { 12.0, 62 } });

        clips::setStart (*p.clip, 0.0, 0.25);
        CHECK (played (*p.clip) == Played { { 0.0, 62 }, { 4.0, 62 }, { 8.0, 62 }, { 12.0, 62 } });

        clips::setStart (*p.clip, 5.0, 0.25);
        CHECK (played (*p.clip) == Played { { 8.0, 62 }, { 12.0, 62 } });

        // Split: both halves loop, nothing moves.
        auto* second = clips::split (*p.clip, 10.0);
        REQUIRE (second != nullptr);
        auto* secondMidi = dynamic_cast<te::MidiClip*> (second);
        REQUIRE (secondMidi != nullptr);
        CHECK (clips::isLooping (*secondMidi));
        CHECK (played (*p.clip) == Played { { 8.0, 62 } });
        CHECK (played (*secondMidi) == Played { { 12.0, 62 } });

        const auto loop = clips::getLoopSongRange (*secondMidi);
        CHECK (loop.getStart() <= 10.0 + 1.0e-9);
        CHECK (loop.getEnd() > 10.0);
        CHECK (std::fmod (loop.getStart(), 4.0) == doctest::Approx (0.0));
    }

    TEST_CASE ("the loop grows when notes go past it, or is set; it is at least one time round")
    {
        OneBar f;
        clips::setLooping (*f.clip, true);
        clips::setEnd (*f.clip, 16.0, 0.25);

        // A note written at content beat 5: the loop becomes two bars.
        core::NoteEditor (*f.clip).add (makeNote (72, 5.0));
        clips::extendToCover (*f.clip, 6.0);
        CHECK (clips::getLoopRange (*f.clip) == juce::Range<double> (0.0, 8.0));
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (16.0));
        CHECK (played (*f.clip) == Played { { 0.0, 60 }, { 2.0, 64 }, { 4.0, 67 }, { 5.0, 72 },
                                            { 8.0, 60 }, { 10.0, 64 }, { 12.0, 67 }, { 13.0, 72 } });

        clips::setLoopLengthBeats (*f.clip, 2.0);
        CHECK (played (*f.clip).size() == 8);       // the note at 0, every two beats
        CHECK (played (*f.clip)[1] == std::pair { 2.0, 60 });

        // A loop longer than the clip: the clip grows to hold it once.
        clips::setEnd (*f.clip, 2.0, 0.25);
        clips::setLoopLengthBeats (*f.clip, 8.0);
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (8.0));
    }

    TEST_CASE ("a note sticking out of the loop is cut at its edges, as Tracktion plays it")
    {
        OneBar f;
        core::NoteEditor (*f.clip).add (makeNote (70, 3.5, 1.0));      // past the loop's end
        clips::setLooping (*f.clip, true);
        clips::setEnd (*f.clip, 8.0, 0.25);

        for (const auto& p : clips::getPlayedNotes (*f.clip))
            if (p.note.pitch == 70)
                CHECK (p.endBeat - p.startBeat == doctest::Approx (0.5));
    }

    TEST_CASE ("the repeats written as notes sound the same; a linked copy keeps its notes")
    {
        OneBar f;
        clips::setLooping (*f.clip, true);
        clips::setEnd (*f.clip, 12.0, 0.25);

        auto* copy = core::clip_links::duplicate (*f.clip, 16.0, true);
        REQUIRE (copy != nullptr);
        REQUIRE (core::clip_links::isLinked (*f.clip));
        const auto before = played (*f.clip);
        f.project->clearUndoHistory();

        f.step ("Bake");
        clips::bakeLoop (*f.clip);
        CHECK_FALSE (clips::isLooping (*f.clip));
        CHECK_FALSE (core::clip_links::isLinked (*f.clip));
        CHECK (played (*f.clip) == before);
        CHECK (f.clip->getSequence().getNotes().size() == 6);
        CHECK (copy->getSequence().getNotes().size() == 3);     // its own notes, still looping
        CHECK (clips::isLooping (*copy));

        f.project->undo();
        CHECK (clips::isLooping (*f.clip));
        CHECK (f.clip->getSequence().getNotes().size() == 3);
        CHECK (played (*f.clip) == before);
    }

    TEST_CASE ("looping is saved; a MIDI file of the clip has every time round")
    {
        auto& session = test::session();
        juce::TemporaryFile file (".harmonia"), midi (".mid");

        {
            OneBar f;
            clips::setLooping (*f.clip, true);
            clips::setEnd (*f.clip, 16.0, 0.25);
            REQUIRE (f.project->save (file.getFile()).isEmpty());
            REQUIRE (core::midi_files::writeClip (*f.clip, midi.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = core::Project::load (session, file.getFile(), error);
        REQUIRE (loaded != nullptr);
        auto* clip = clips::getMidiClips (*loaded->getTracks().getFirst()).getFirst();
        REQUIRE (clip != nullptr);
        CHECK (clips::isLooping (*clip));
        CHECK (played (*clip).size() == 8);

        const auto contents = core::midi_files::read (midi.getFile(), error);
        REQUIRE (contents.has_value());
        size_t notes = 0;

        for (const auto& part : contents->parts)
            notes += part.notes.size();

        CHECK (notes == 8);
    }
}
