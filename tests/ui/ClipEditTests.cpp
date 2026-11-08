#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Instruments.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/TestProject.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui::arrangement;
namespace clips = core::clips;
namespace links = core::clip_links;
namespace tracks = core::tracks;

namespace
{
    void dispatch()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    core::Note makeNote (int pitch, double start)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = 1.0;
        return n;
    }

    /** One 4-bar clip with two notes on the first track, a second instrument track and an audio
        track with a 4-second (8-beat) audio clip at bar 2. */
    struct EditFixture : test::ProjectWithClip
    {
        EditFixture()
        {
            core::NoteEditor (*clip).add (makeNote (60, 2.0));
            core::NoteEditor (*clip).add (makeNote (64, 10.0));
            second = project->addTrack ("Second");
            audioTrack = project->addTrack ("Audio");
            tracks::setKind (*audioTrack, tracks::Kind::audio);

            juce::String error;
            audioClip = project->addAudioClip (*audioTrack, test::writeTone (temp.getFile ("Tone.wav"), 4.0), 4.0, error);
            REQUIRE (audioClip != nullptr);
            dispatch();
            project->clearUndoHistory();
        }

        /** Notes of a clip, as song beats. */
        std::vector<double> songBeatsOf (te::MidiClip& c) const
        {
            std::vector<double> beats;

            for (const auto& n : test::readNotes (c))
                beats.push_back (n.startBeat + clips::getContentStartBeat (c));

            return beats;
        }

        test::TempFolder temp;
        te::AudioTrack* second = nullptr;
        te::AudioTrack* audioTrack = nullptr;
        te::WaveAudioClip* audioClip = nullptr;
        ArrangementModel model { *project };
    };
}

TEST_SUITE ("arrangement: split")
{
    TEST_CASE ("a MIDI clip splits in two where asked; nothing moves in the song; one undo step")
    {
        EditFixture f;
        CHECK (f.model.splitAt (6.0) == 1);   // the current track's clip (nothing selected)
        dispatch();

        const auto onTrack = clips::getMidiClips (*f.track);
        REQUIRE (onTrack.size() == 2);
        CHECK (clips::getStartBeat (*onTrack[0]) == doctest::Approx (0.0));
        CHECK (clips::getEndBeat (*onTrack[0]) == doctest::Approx (6.0));
        CHECK (clips::getStartBeat (*onTrack[1]) == doctest::Approx (6.0));
        CHECK (clips::getEndBeat (*onTrack[1]) == doctest::Approx (16.0));

        // The notes stay where they were in the song; each half plays its part.
        CHECK (clips::getContentStartBeat (*onTrack[1]) == doctest::Approx (0.0));
        CHECK (f.songBeatsOf (*onTrack[1]) == f.songBeatsOf (*onTrack[0]));
        CHECK_FALSE (links::isLinked (*onTrack[1]));
        CHECK (f.model.getSelectedClips() == juce::Array<te::Clip*> { onTrack[1] });

        CHECK (f.project->getEdit().getUndoManager().getUndoDescription() == "Split clip");
        f.model.undo();
        dispatch();
        REQUIRE (clips::getMidiClips (*f.track).size() == 1);
        CHECK (clips::getEndBeat (*clips::getMidiClips (*f.track)[0]) == doctest::Approx (16.0));
    }

    TEST_CASE ("an audio clip splits with its sound in place")
    {
        EditFixture f;
        f.model.setSelection ({ f.audioClip });
        CHECK (f.model.splitAt (7.0) == 1);
        dispatch();

        const auto onTrack = clips::getClips (*f.audioTrack);
        REQUIRE (onTrack.size() == 2);
        CHECK (clips::getEndBeat (*onTrack[0]) == doctest::Approx (7.0));
        CHECK (clips::getStartBeat (*onTrack[1]) == doctest::Approx (7.0));
        CHECK (clips::getEndBeat (*onTrack[1]) == doctest::Approx (12.0));
        // The right half starts 3 beats (1.5 s) into the file.
        CHECK (onTrack[1]->getPosition().getOffset().inSeconds() == doctest::Approx (1.5));
    }

    TEST_CASE ("selected clips are split rather than the current track's; nothing there, nothing split")
    {
        EditFixture f;
        CHECK (f.model.splitAt (16.0) == 0);    // at the clip's end
        CHECK (f.model.splitAt (40.0) == 0);    // after everything

        // A selected clip elsewhere decides; the current track's clip stays whole.
        f.model.setSelection ({ f.audioClip });
        CHECK (f.model.splitAt (8.0) == 1);
        dispatch();
        CHECK (clips::getMidiClips (*f.track).size() == 1);
        CHECK (clips::getClips (*f.audioTrack).size() == 2);

        // A selection away from the split point: the current track's clips are split.
        f.model.setSelection ({ f.audioClip });
        CHECK (f.model.splitAt (2.0) == 1);
        dispatch();
        CHECK (clips::getMidiClips (*f.track).size() == 2);
    }

    TEST_CASE ("the halves of a linked clip stay linked")
    {
        EditFixture f;
        f.model.setSelection ({ f.clip });
        f.model.duplicateSelected (true);
        f.model.setSelection ({ f.clip });
        CHECK (f.model.splitAt (8.0) == 1);
        dispatch();

        const auto onTrack = clips::getMidiClips (*f.track);
        REQUIRE (onTrack.size() == 3);
        CHECK (links::getGroup (*f.clip).size() == 3);
    }
}

TEST_SUITE ("arrangement: clipboard")
{
    TEST_CASE ("copied clips paste as independent copies at a beat, keeping their places to each other")
    {
        EditFixture f;
        auto* onSecond = f.project->addMidiClip (*f.second, 4.0, 4.0, "B");
        REQUIRE (onSecond != nullptr);
        dispatch();
        f.project->clearUndoHistory();

        f.model.setSelection ({ f.clip, onSecond });
        CHECK (f.model.copySelected() == 2);
        CHECK (ArrangementModel::canPaste());
        CHECK (ArrangementModel::getNumCopiedClips() == 2);

        // Pasted with the first track current: same tracks, 32 beats later.
        f.model.setCurrentTrack (f.track);
        CHECK (f.model.paste (32.0) == 2);
        dispatch();
        CHECK (f.project->getEdit().getUndoManager().getUndoDescription() == "Paste clips");

        const auto onFirst = clips::getMidiClips (*f.track);
        const auto onSecondTrack = clips::getMidiClips (*f.second);
        REQUIRE (onFirst.size() == 2);
        REQUIRE (onSecondTrack.size() == 2);
        CHECK (clips::getStartBeat (*onFirst[1]) == doctest::Approx (32.0));
        CHECK (clips::getEndBeat (*onFirst[1]) == doctest::Approx (48.0));
        CHECK (clips::getStartBeat (*onSecondTrack[1]) == doctest::Approx (36.0));
        CHECK (test::readNotes (*onFirst[1]) == test::readNotes (*f.clip));
        CHECK (f.model.getSelectedClips().size() == 2);

        // Independent: the copy's notes are its own.
        core::NoteEditor (*onFirst[1]).add (makeNote (72, 0.0));
        CHECK (test::readNotes (*f.clip).size() == 2);

        f.model.undo();
        dispatch();
        f.model.undo();
        dispatch();
        CHECK (clips::getMidiClips (*f.track).size() == 1);
        CHECK (clips::getMidiClips (*f.second).size() == 1);
    }

    TEST_CASE ("pasting lands on the current track; a clip goes down to the next track of its kind, or is left out")
    {
        EditFixture f;
        f.model.setSelection ({ f.clip, f.audioClip });
        CHECK (f.model.copySelected() == 2);

        // Current: the second track. The MIDI clip goes there, the audio clip (two tracks lower when
        // copied) has no track below the audio track: left out.
        f.model.setCurrentTrack (f.second);
        CHECK (f.model.paste (16.0) == 1);
        dispatch();
        CHECK (clips::getMidiClips (*f.second).size() == 1);
        CHECK (clips::getClips (*f.audioTrack).size() == 1);

        // Copying only the audio clip and pasting with the first track current: it goes down to the
        // audio track.
        f.model.setSelection ({ f.audioClip });
        f.model.copySelected();
        f.model.setCurrentTrack (f.track);
        CHECK (f.model.paste (20.0) == 1);
        dispatch();
        const auto audioClips = clips::getClips (*f.audioTrack);
        REQUIRE (audioClips.size() == 2);
        CHECK (clips::getStartBeat (*audioClips[1]) == doctest::Approx (20.0));
        CHECK (clips::isAudio (*audioClips[1]));
    }

    TEST_CASE ("cut takes the clips away; undo brings them back; paste puts them somewhere else")
    {
        EditFixture f;
        f.model.setSelection ({ f.clip });
        CHECK (f.model.cutSelected() == 1);
        dispatch();
        CHECK (clips::getMidiClips (*f.track).isEmpty());

        f.model.setCurrentTrack (f.second);
        CHECK (f.model.paste (8.0) == 1);
        dispatch();
        REQUIRE (clips::getMidiClips (*f.second).size() == 1);
        CHECK (clips::getStartBeat (*clips::getMidiClips (*f.second)[0]) == doctest::Approx (8.0));
        CHECK (test::readNotes (*clips::getMidiClips (*f.second)[0]).size() == 2);

        f.model.undo();
        dispatch();
        f.model.undo();
        dispatch();
        CHECK (clips::getMidiClips (*f.track).size() == 1);
        CHECK (clips::getMidiClips (*f.second).isEmpty());
    }

    TEST_CASE ("a pasted copy of a linked clip is on its own; the group stays")
    {
        EditFixture f;
        f.model.setSelection ({ f.clip });
        f.model.duplicateSelected (true);
        REQUIRE (links::getGroup (*f.clip).size() == 2);

        f.model.setSelection ({ f.clip });
        f.model.copySelected();
        f.model.setCurrentTrack (f.second);
        REQUIRE (f.model.paste (0.0) == 1);
        dispatch();

        auto* pasted = clips::getMidiClips (*f.second).getFirst();
        REQUIRE (pasted != nullptr);
        CHECK_FALSE (links::isLinked (*pasted));
        CHECK (links::getGroup (*f.clip).size() == 2);
    }
}

TEST_SUITE ("arrangement: edit keys")
{
    TEST_CASE ("Ctrl+C, Ctrl+V and Ctrl+E work in the arrangement, with the Russian layout too")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        audio::instruments::useBuiltInSynth (track);
        auto* clip = project->addMidiClip (track, 0.0, 16.0, "Main");
        project->clearUndoHistory();
        auto main = std::make_unique<ui::MainComponent> (test::session(), *project);
        main->setBounds (0, 0, 1280, 800);
        juce::StringArray messages;
        auto& arrangement = main->getArrangement();
        arrangement.onMessage = [&messages] (const juce::String& text, bool) { messages.add (text); };
        auto& model = main->getArrangementModel();
        model.setSnap (core::snap::Mode::bar);

        const auto press = [&arrangement] (int code, juce::juce_wchar text)
        {
            return arrangement.keyPressed (juce::KeyPress (code, juce::ModifierKeys::ctrlModifier, text));
        };

        // Ctrl+E with the playhead in the clip: split there.
        main->getTransport().setPositionSeconds (2.0);   // beat 4
        CHECK (press ('E', 0x0443));                      // У: the E key
        dispatch();
        REQUIRE (core::clips::getMidiClips (track).size() == 2);
        CHECK (core::clips::getEndBeat (*clip) == doctest::Approx (4.0));

        // Ctrl+C on the right half, Ctrl+V at the playhead (snapped down to the bar).
        model.setSelection ({ core::clips::getMidiClips (track)[1] });
        CHECK (press ('C', 0x0441));                      // С
        CHECK (messages.joinIntoString ("|").contains ("Copied 1 clip"));
        main->getTransport().setPositionSeconds (17.0);   // beat 34 -> bar at 32
        CHECK (press (0x043c, 0));                        // м: the V key, as typed on other systems
        dispatch();
        const auto all = core::clips::getMidiClips (track);
        REQUIRE (all.size() == 3);
        CHECK (core::clips::getStartBeat (*all[2]) == doctest::Approx (32.0));

        // Ctrl+X takes it away again.
        model.setSelection ({ all[2] });
        CHECK (press ('X', 0x0447));                      // Ч
        dispatch();
        CHECK (core::clips::getMidiClips (track).size() == 2);

        // Nothing to split where the playhead is: said so.
        main->getTransport().setPositionSeconds (30.0);
        model.deselectAll();
        CHECK (press ('E', 0));
        CHECK (messages.joinIntoString ("|").contains ("Nothing to split"));

        main.reset();
    }
}
