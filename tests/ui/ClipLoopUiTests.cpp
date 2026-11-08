#include <doctest.h>

#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"
#include "ui/NoteInput.h"

using namespace harmonia;
using namespace harmonia::ui;
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

    bool runItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            const auto& item = it.getItem();

            if (item.text == text && item.isEnabled && item.action != nullptr)
            {
                item.action();
                return true;
            }
        }

        return false;
    }

    std::optional<bool> isTicked (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == text)
                return it.getItem().isTicked;

        return std::nullopt;
    }

    struct FakeClock final : audio::SongClock
    {
        bool isPlaying() const override                             { return playing; }
        std::optional<double> getHeardBeat (double) const override  { return playing ? std::optional<double> (beat) : std::nullopt; }
        juce::Range<double> getLoopBeats() const override           { return loop; }

        bool playing = false;
        double beat = 0.0;
        juce::Range<double> loop { 0.0, 16.0 };
    };

    /** The 4-bar test clip looping its first bar (notes at 0 and 2). */
    struct LoopingClip : test::ProjectWithClip
    {
        LoopingClip()
        {
            core::NoteEditor (*clip).add ({ makeNote (60, 0.0), makeNote (64, 2.0) });
            clips::setEnd (*clip, 4.0, 0.25);
            clips::setLooping (*clip, true);
            clips::setEnd (*clip, 16.0, 0.25);
            project->clearUndoHistory();
        }
    };
}

TEST_SUITE ("looping clips in the piano roll and the arrangement")
{
    TEST_CASE ("the piano roll edits the loop; its repeats show as ghost notes; writing past it makes it longer")
    {
        LoopingClip f;
        pianoroll::PianoRollModel model (*f.project);
        model.setClip (f.clip);

        CHECK (model.isLooping());
        CHECK (model.getClipStartBeat() == doctest::Approx (0.0));
        CHECK (model.getClipEndBeat() == doctest::Approx (4.0));
        CHECK (model.getNotes().size() == 2);
        CHECK (model.getGhostNotes().size() == 6);       // three more times round

        // A note in the second bar: the loop becomes two bars, played twice.
        model.beginGesture ("Add");
        model.addNote (makeNote (67, 5.0));
        CHECK (clips::getLoopRange (*f.clip) == juce::Range<double> (0.0, 8.0));
        CHECK (model.getClipEndBeat() == doctest::Approx (8.0));
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (16.0));
        CHECK (model.getGhostNotes().size() == 3);

        f.project->undo();
        CHECK (clips::getLoopRange (*f.clip) == juce::Range<double> (0.0, 4.0));
        CHECK (model.getGhostNotes().size() == 6);
    }

    TEST_CASE ("the clip menu loops the clip and writes the repeats as notes, one undo step each; clip mode plays the loop")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& model = main.getArrangementModel();

        auto* clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 4.0, "Beat");
        core::NoteEditor (*clip).add ({ makeNote (36, 0.0, 0.5), makeNote (38, 1.0, 0.5), makeNote (36, 2.5, 0.5), makeNote (38, 3.0, 0.5) });
        model.openClip (clip);
        project->clearUndoHistory();

        auto& arrangement = main.getArrangement();
        auto menu = arrangement.createClipMenu (*clip);
        CHECK (isTicked (menu, "Loop (repeats when made longer)") == false);
        CHECK_FALSE (runItem (menu, "Make the repeats real notes"));      // not looping yet
        CHECK (runItem (menu, "Loop (repeats when made longer)"));
        CHECK (clips::isLooping (*clip));

        project->beginGesture ("Longer");
        clips::setEnd (*clip, 16.0, 0.25);
        CHECK (clips::getPlayedNotes (*clip).size() == 16);

        // Clip mode plays the loop, as the piano roll shows it.
        model.setPlayMode (arrangement::PlayMode::clip);
        CHECK (model.getPlayRange().start == doctest::Approx (0.0));
        CHECK (model.getPlayRange().end == doctest::Approx (4.0));

        menu = arrangement.createClipMenu (*clip);
        CHECK (isTicked (menu, "Loop (repeats when made longer)") == true);
        CHECK (runItem (menu, "Make the repeats real notes"));
        CHECK_FALSE (clips::isLooping (*clip));
        CHECK (clip->getSequence().getNotes().size() == 16);
        CHECK (model.getPlayRange().end == doctest::Approx (16.0));

        project->undo();
        CHECK (clips::isLooping (*clip));
        CHECK (clip->getSequence().getNotes().size() == 4);
        project->undo();
        project->undo();
        CHECK_FALSE (clips::isLooping (*clip));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (4.0));
    }

    TEST_CASE ("notes played over a looping clip go into its loop")
    {
        LoopingClip f;
        pianoroll::PianoRollModel model (*f.project);
        model.setClip (f.clip);
        audio::NotePreview preview;
        FakeClock clock;

        {
            NoteInput input (model, preview, clock);
            input.setRecordArmed (true);
            clock.playing = true;
            input.update();
            REQUIRE (input.isRecording());

            // The third time round (song beats 8..12): beat 9 is the loop's beat 1.
            clock.beat = 9.0;
            input.noteOn (62, 100, NoteInput::Source::midiKeyboard);
            clock.beat = 9.5;
            input.noteOff (62, NoteInput::Source::midiKeyboard);

            clock.playing = false;
            input.update();
        }

        bool found = false;

        for (const auto& n : test::readNotes (*f.clip))
        {
            if (n.pitch == 62)
            {
                found = true;
                CHECK (n.startBeat == doctest::Approx (1.0));
                CHECK (n.lengthBeats == doctest::Approx (0.5));
            }
        }

        CHECK (found);
        CHECK (clips::getLoopRange (*f.clip) == juce::Range<double> (0.0, 4.0));
    }

    TEST_CASE ("the main window paints looping clips and the repeats in the piano roll")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& model = main.getArrangementModel();
        auto& track = *project->getTracks().getFirst();
        track.setName ("Drums");

        auto* beat = project->addMidiClip (track, 0.0, 4.0, "Beat");
        core::NoteEditor (*beat).add ({ makeNote (36, 0.0, 0.5), makeNote (42, 0.5, 0.25), makeNote (38, 1.0, 0.5),
                                        makeNote (42, 1.5, 0.25), makeNote (36, 2.5, 0.5), makeNote (38, 3.0, 0.5),
                                        makeNote (42, 3.5, 0.25) });
        model.setLooping (*beat, true);
        clips::setEnd (*beat, 32.0, 0.25);

        auto* bassTrack = model.addTrack();
        model.renameTrack (*bassTrack, "Bass");
        auto* bass = model.getOpenClip();
        core::NoteEditor (*bass).add ({ makeNote (33, 0.0, 1.5), makeNote (36, 2.0, 1.0), makeNote (40, 4.0, 1.5), makeNote (38, 6.0, 2.0) });
        clips::setEnd (*bass, 8.0, 0.25);
        model.setLooping (*bass, true);
        clips::setEnd (*bass, 32.0, 0.25);

        model.openClip (beat);
        main.getPianoRoll().showClip();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-loop.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
