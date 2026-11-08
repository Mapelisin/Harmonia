#include <doctest.h>

#include "TestProject.h"

using namespace harmonia;
using core::Note;

TEST_SUITE ("core::NoteEditor")
{
    TEST_CASE ("add, move and remove are undone and redone step by step")
    {
        test::ProjectWithClip p;
        auto& project = *p.project;
        core::NoteEditor editor (*p.clip);

        Note original;
        original.pitch = 60;
        original.startBeat = 1.0;

        Note moved = original;
        moved.pitch = 67;
        moved.startBeat = 3.0;

        project.beginGesture ("Add note");
        editor.add (original);

        project.beginGesture ("Move note");
        editor.update (*editor.getNotes().getFirst(), moved);

        project.beginGesture ("Delete note");
        editor.remove (*editor.getNotes().getFirst());

        CHECK (test::readNotes (*p.clip).empty());

        project.undo();
        CHECK (test::readNotes (*p.clip) == std::vector<Note> { moved });

        project.undo();
        CHECK (test::readNotes (*p.clip) == std::vector<Note> { original });

        project.undo();
        CHECK (test::readNotes (*p.clip).empty());

        project.redo();
        project.redo();
        CHECK (test::readNotes (*p.clip) == std::vector<Note> { moved });

        project.redo();
        CHECK (test::readNotes (*p.clip).empty());
        CHECK_FALSE (project.canRedo());
    }

    TEST_CASE ("a drag made of many small moves is undone in one step")
    {
        test::ProjectWithClip p;
        auto& project = *p.project;
        core::NoteEditor editor (*p.clip);

        project.beginGesture ("Add note");
        auto* n = editor.add (Note {});
        REQUIRE (n != nullptr);

        project.beginGesture ("Drag note");

        for (int step = 1; step <= 50; ++step)
        {
            auto values = core::note::read (*n);
            values.startBeat = step * 0.125;
            editor.update (*n, values);
        }

        CHECK (core::note::read (*n).startBeat == doctest::Approx (6.25));

        project.undo();
        CHECK (test::readNotes (*p.clip) == std::vector<Note> { Note {} });
    }

    TEST_CASE ("batch operations change every selected note")
    {
        test::ProjectWithClip p;
        core::NoteEditor editor (*p.clip);

        std::vector<Note> chord;

        for (int pitch : { 60, 64, 67, 71 })
        {
            Note n;
            n.pitch = pitch;
            chord.push_back (n);
        }

        auto added = editor.add (chord);
        REQUIRE (added.size() == 4);

        std::vector<core::NoteEditor::Change> changes;

        for (auto* n : added)
        {
            auto values = core::note::read (*n);
            values.pitch += 12;
            changes.push_back ({ n, values });
        }

        editor.update (changes);

        std::vector<int> pitches;

        for (const auto& n : test::readNotes (*p.clip))
            pitches.push_back (n.pitch);

        CHECK (pitches == std::vector<int> { 72, 76, 79, 83 });

        editor.remove (juce::Array<te::MidiNote*> { editor.getNotes()[1], editor.getNotes()[2] });

        pitches.clear();

        for (const auto& n : test::readNotes (*p.clip))
            pitches.push_back (n.pitch);

        CHECK (pitches == std::vector<int> { 72, 83 });
    }
}
