#include <doctest.h>

#include "TestProject.h"
#include "core/ClipLinks.h"

using namespace harmonia;
using core::Note;
namespace links = core::clip_links;

namespace
{
    double getStartBeat (const te::Clip& clip)
    {
        return clip.edit.tempoSequence.toBeats (clip.getPosition().getStart()).inBeats();
    }
}

TEST_SUITE ("core::clip_links")
{
    TEST_CASE ("a linked copy starts where asked and shares the notes")
    {
        test::ProjectWithClip p;
        core::NoteEditor editor (*p.clip);
        editor.add (test::makeRichNote (60, 0.0));

        auto* copy = links::duplicate (*p.clip, 16.0, true);

        REQUIRE (copy != nullptr);
        CHECK (copy != p.clip);
        CHECK (getStartBeat (*copy) == doctest::Approx (16.0));
        CHECK (links::isLinked (*p.clip));
        CHECK (links::isLinked (*copy));
        CHECK (links::getGroup (*p.clip).size() == 2);
        CHECK (test::readNotes (*copy) == test::readNotes (*p.clip));
    }

    TEST_CASE ("editing either copy changes all of them")
    {
        test::ProjectWithClip p;
        auto* second = links::duplicate (*p.clip, 16.0, true);
        auto* third = links::duplicate (*second, 32.0, true);
        REQUIRE (third != nullptr);
        CHECK (links::getGroup (*p.clip).size() == 3);

        core::NoteEditor first (*p.clip);
        first.add (test::makeRichNote (62, 1.0));

        CHECK (test::readNotes (*second) == test::readNotes (*p.clip));
        CHECK (test::readNotes (*third) == test::readNotes (*p.clip));

        core::NoteEditor viaThird (*third);
        auto values = core::note::read (*viaThird.getNotes().getFirst());
        values.pitch = 50;
        values.pan = 0.5f;
        viaThird.update (*viaThird.getNotes().getFirst(), values);
        viaThird.add (Note {});

        CHECK (test::readNotes (*p.clip).size() == 2);
        CHECK (test::readNotes (*p.clip) == test::readNotes (*third));
        CHECK (test::readNotes (*second) == test::readNotes (*third));
    }

    TEST_CASE ("undo and redo restore every copy exactly")
    {
        test::ProjectWithClip p;
        auto& project = *p.project;
        core::NoteEditor editor (*p.clip);

        project.beginGesture ("Add note");
        editor.add (test::makeRichNote (60, 0.0));

        project.beginGesture ("Duplicate linked");
        auto* copy = links::duplicate (*p.clip, 16.0, true);
        REQUIRE (copy != nullptr);
        const auto copyState = copy->state;

        const auto before = test::readNotes (*p.clip);

        // Edit through the copy, then through the original, in separate gestures.
        project.beginGesture ("Edit copy");
        core::NoteEditor copyEditor (*copy);
        copyEditor.add (Note {});

        project.beginGesture ("Edit original");
        editor.remove (*editor.getNotes().getFirst());

        const auto after = test::readNotes (*p.clip);
        CHECK (test::readNotes (*copy) == after);

        project.undo();
        project.undo();
        CHECK (test::readNotes (*p.clip) == before);
        CHECK (test::readNotes (*copy) == before);

        project.redo();
        project.redo();
        CHECK (test::readNotes (*p.clip) == after);
        CHECK (test::readNotes (*copy) == after);

        // Undoing the duplicate removes the copy and the original is no longer linked.
        project.undo();
        project.undo();
        project.undo();
        CHECK_FALSE (copyState.getParent().isValid());
        CHECK_FALSE (links::isLinked (*p.clip));
    }

    TEST_CASE ("unlinking makes a copy independent and dissolves a group of one")
    {
        test::ProjectWithClip p;
        auto* copy = links::duplicate (*p.clip, 16.0, true);
        REQUIRE (copy != nullptr);

        links::unlink (*copy);

        CHECK_FALSE (links::isLinked (*copy));
        CHECK_FALSE (links::isLinked (*p.clip));

        core::NoteEditor (*p.clip).add (Note {});

        CHECK (test::readNotes (*p.clip).size() == 1);
        CHECK (test::readNotes (*copy).empty());
    }

    TEST_CASE ("a plain duplicate is independent")
    {
        test::ProjectWithClip p;
        core::NoteEditor (*p.clip).add (Note {});

        auto* copy = links::duplicate (*p.clip, 16.0, false);
        REQUIRE (copy != nullptr);

        CHECK_FALSE (links::isLinked (*copy));
        CHECK (test::readNotes (*copy) == test::readNotes (*p.clip));

        core::NoteEditor (*copy).add (test::makeRichNote (70, 2.0));

        CHECK (test::readNotes (*copy).size() == 2);
        CHECK (test::readNotes (*p.clip).size() == 1);
    }
}
