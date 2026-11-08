#include <doctest.h>

#include "TestProject.h"

using namespace harmonia;
using core::Note;

TEST_SUITE ("core::Note")
{
    TEST_CASE ("a default note stores no Harmonia attributes")
    {
        const auto state = core::note::createState (Note {});

        for (int i = 0; i < state.getNumProperties(); ++i)
            CHECK_FALSE (state.getPropertyName (i).toString().startsWith ("h"));
    }

    TEST_CASE ("every property reads back as written")
    {
        test::ProjectWithClip p;
        core::NoteEditor editor (*p.clip);

        const auto values = test::makeRichNote (64, 2.5);
        auto* added = editor.add (values);

        REQUIRE (added != nullptr);
        CHECK (core::note::read (*added) == values);
    }

    TEST_CASE ("a default note reads back as default")
    {
        test::ProjectWithClip p;
        core::NoteEditor editor (*p.clip);

        auto* added = editor.add (Note {});

        REQUIRE (added != nullptr);
        CHECK (core::note::read (*added) == Note {});
    }

    TEST_CASE ("out-of-range values are clamped")
    {
        Note n;
        n.pitch = 200;
        n.startBeat = -3.0;
        n.lengthBeats = -1.0;
        n.velocity = 500;
        n.releaseVelocity = -5;
        n.colour = 40;
        n.pan = 7.0f;
        n.modX = -1.0f;
        n.modY = 2.0f;
        n.finePitchCents = -5000.0f;
        n.group = -2;

        const auto c = n.clamped();

        CHECK (c.pitch == 127);
        CHECK (c.startBeat == 0.0);
        CHECK (c.lengthBeats == 0.0);
        CHECK (c.velocity == 127);
        CHECK (c.releaseVelocity == 0);
        CHECK (c.colour == core::note::numColours - 1);
        CHECK (c.pan == 1.0f);
        CHECK (c.modX == 0.0f);
        CHECK (c.modY == 1.0f);
        CHECK (c.finePitchCents == -core::note::maxFinePitchCents);
        CHECK (c.group == 0);
    }

    TEST_CASE ("setting a property back to its default removes the attribute")
    {
        test::ProjectWithClip p;
        core::NoteEditor editor (*p.clip);

        auto* added = editor.add (test::makeRichNote (60, 0.0));
        REQUIRE (added != nullptr);
        CHECK (added->state.hasProperty ("hPan"));

        auto values = core::note::read (*added);
        values.pan = 0.0f;
        editor.update (*added, values);

        CHECK_FALSE (added->state.hasProperty ("hPan"));
        CHECK (core::note::read (*added) == values);
    }
}
