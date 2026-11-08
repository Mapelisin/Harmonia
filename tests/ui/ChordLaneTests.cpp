#include <doctest.h>

#include "core/NoteEditor.h"
#include "core/Scales.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace chords = core::chords;
using chords::Span;

namespace
{
    core::Note makeNote (int pitch, double start, double length)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }

    /** A bar of each chord (whole notes), from `start`. */
    void addChords (std::vector<core::Note>& notes, std::initializer_list<std::initializer_list<int>> progression, double start = 0.0)
    {
        for (const auto& chord : progression)
        {
            for (auto pitch : chord)
                notes.push_back (makeNote (pitch, start, 4.0));

            start += 4.0;
        }
    }

    bool runItem (const juce::PopupMenu& menu, const juce::String& text, bool* ticked = nullptr)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            const auto& item = it.getItem();

            if (item.text == text && item.isEnabled && item.action != nullptr)
            {
                if (ticked != nullptr)
                    *ticked = item.isTicked;

                item.action();
                return true;
            }
        }

        return false;
    }
}

TEST_SUITE ("chords over time")
{
    TEST_CASE ("a progression: one chord a bar; silence between leaves a gap")
    {
        std::vector<core::Note> notes;
        addChords (notes, { { 48, 64, 67, 72 }, { 45, 60, 64, 69 }, { 41, 60, 65, 69 }, { 43, 62, 67, 71 } });
        CHECK (chords::findChords (notes) == std::vector<Span> { { 0.0, 4.0, "C" }, { 4.0, 8.0, "Am" }, { 8.0, 12.0, "F" }, { 12.0, 16.0, "G" } });

        // The same chord twice in a row is one; a bar of silence is a gap.
        std::vector<core::Note> again;
        addChords (again, { { 60, 64, 67 }, { 60, 64, 67 } });
        addChords (again, { { 57, 60, 64 } }, 12.0);
        CHECK (chords::findChords (again) == std::vector<Span> { { 0.0, 8.0, "C" }, { 12.0, 16.0, "Am" } });

        CHECK (chords::findChords ({}).empty());
        CHECK (chords::findChords ({ makeNote (60, 0.0, 4.0) }).empty());     // one note isn't a chord
    }

    TEST_CASE ("a melody over the chords doesn't hide them; passing notes and muted notes don't count")
    {
        std::vector<core::Note> notes;
        addChords (notes, { { 48, 52, 55 }, { 53, 57, 60 } });     // C, F

        // A melody: D over C (C, E, G, D: Cadd9, a real chord), then F# over C (no chord: the top note
        // is left out), then over F a quick passing B (an eighth note).
        notes.push_back (makeNote (74, 0.0, 2.0));
        notes.push_back (makeNote (78, 2.0, 2.0));
        notes.push_back (makeNote (71, 4.0, 0.5));
        notes.push_back (makeNote (72, 4.5, 3.5));

        auto muted = makeNote (61, 0.0, 8.0);      // would spoil everything if it counted
        muted.muted = true;
        notes.push_back (muted);

        CHECK (chords::findChords (notes) == std::vector<Span> { { 0.0, 2.0, "Cadd9" }, { 2.0, 4.0, "C" }, { 4.0, 8.0, "F" } });
    }

    TEST_CASE ("the piano roll keeps them in step with the notes; the Scale menu hides them")
    {
        test::ProjectWithClip p;
        pianoroll::PianoRollModel model (*p.project);
        model.setClip (p.clip);
        audio::Transport transport (p.project->getEdit());
        pianoroll::PianoRoll pianoRoll (model, transport);
        pianoRoll.setBounds (0, 0, 1280, 600);

        CHECK (model.getChords().empty());
        CHECK (model.areChordNamesShown());

        std::vector<core::Note> notes;
        addChords (notes, { { 57, 60, 64 }, { 55, 59, 62, 65 } });
        core::NoteEditor (*p.clip).add (notes);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK (model.getChords() == std::vector<Span> { { 0.0, 4.0, "Am" }, { 4.0, 8.0, "G7" } });

        bool ticked = false;
        CHECK (runItem (pianoRoll.createScaleMenu(), "Chord names on the ruler", &ticked));
        CHECK (ticked);
        CHECK_FALSE (model.areChordNamesShown());
        CHECK (pianoRoll.createComponentSnapshot (pianoRoll.getLocalBounds()).isValid());
        model.setChordNamesShown (true);
    }

    TEST_CASE ("the main window paints a progression with its chords on the ruler")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& track = *project->getTracks().getFirst();
        auto* clip = project->addMidiClip (track, 0.0, 16.0, "Progression");

        std::vector<core::Note> notes;
        addChords (notes, { { 48, 64, 67, 72 }, { 45, 60, 64, 69 }, { 41, 60, 65, 69 }, { 43, 62, 67, 71 } });

        for (int i = 0; i < 8; ++i)     // a melody of half notes on top
            notes.push_back (makeNote (std::array<int, 8> { 76, 79, 76, 72, 77, 72, 74, 71 }[(size_t) i], i * 2.0, 2.0));

        core::NoteEditor (*clip).add (notes);
        main.getArrangementModel().openClip (clip);
        main.getPianoRoll().zoomToFit();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto& found = main.getPianoRollModel().getChords();
        REQUIRE (found.size() == 4);
        CHECK (found[0].name == "C");
        CHECK (found[3].name == "G");

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-chord-lane.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
