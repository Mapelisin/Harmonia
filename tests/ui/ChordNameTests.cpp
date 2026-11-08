#include <doctest.h>

#include "core/NoteEditor.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;

namespace
{
    core::Note makeNote (int pitch, double start, double length = 4.0)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }

    void settle()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
}

TEST_SUITE ("chord names")
{
    TEST_CASE ("the piano roll names the chord the selected notes make")
    {
        test::ProjectWithClip p;
        core::NoteEditor (*p.clip).add ({ makeNote (57, 0.0), makeNote (60, 0.0), makeNote (64, 0.0), makeNote (67, 0.0),
                                          makeNote (62, 4.0) });

        pianoroll::PianoRollModel model (*p.project);
        model.setClip (p.clip);
        audio::Transport transport (p.project->getEdit());
        pianoroll::PianoRoll pianoRoll (model, transport);
        pianoRoll.setBounds (0, 0, 1280, 600);
        settle();

        CHECK (pianoRoll.getInfoText().contains ("5 notes"));
        CHECK_FALSE (pianoRoll.getInfoText().contains ("Chord"));

        auto select = [&] (std::initializer_list<int> pitches)
        {
            model.deselectAll();

            for (auto* note : p.clip->getSequence().getNotes())
                if (std::find (pitches.begin(), pitches.end(), note->getNoteNumber()) != pitches.end())
                    model.setSelected (*note, true);

            settle();
        };

        select ({ 57, 60, 64, 67 });
        CHECK (pianoRoll.getInfoText() == juce::String::fromUTF8 ("Chord Am7  \xc2\xb7  4 selected"));

        select ({ 60, 64, 67 });
        CHECK (pianoRoll.getInfoText() == juce::String::fromUTF8 ("Chord C  \xc2\xb7  3 selected"));

        // Two notes that aren't a chord, or one note: the clip's size and the count, as before.
        select ({ 60, 62 });
        CHECK (pianoRoll.getInfoText().endsWith ("5 notes (2 selected)"));
        select ({ 62 });
        CHECK (pianoRoll.getInfoText().endsWith ("5 notes (1 selected)"));
    }
}
