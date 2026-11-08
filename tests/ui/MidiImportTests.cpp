#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Instruments.h"
#include "core/Drums.h"
#include "core/Clips.h"
#include "core/MidiFiles.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/arrangement/ArrangementModel.h"

using namespace harmonia;
using namespace harmonia::ui::arrangement;
namespace clips = core::clips;

namespace
{
    void dispatch()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    /** A MIDI file with `numParts` tracks of two notes each over 6 beats, at 95 BPM in 3/4. */
    juce::File writeFile (const juce::File& file, int numParts)
    {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (96);

        juce::MidiMessageSequence conductor;
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / 95.0)), 0.0);
        conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (3, 4), 0.0);
        midi.addTrack (conductor);

        for (int p = 0; p < numParts; ++p)
        {
            juce::MidiMessageSequence part;
            part.addEvent (juce::MidiMessage::textMetaEvent (3, "Part " + juce::String::charToString ((juce::juce_wchar) ('A' + p))), 0.0);
            part.addEvent (juce::MidiMessage::noteOn (1, 60 + p, (juce::uint8) 100), 0.0);
            part.addEvent (juce::MidiMessage::noteOff (1, 60 + p), 96.0);
            part.addEvent (juce::MidiMessage::noteOn (1, 64 + p, (juce::uint8) 100), 96.0 * 4);
            part.addEvent (juce::MidiMessage::noteOff (1, 64 + p), 96.0 * 6);
            midi.addTrack (part);
        }

        juce::FileOutputStream out (file);
        REQUIRE (out.openedOk());
        REQUIRE (midi.writeTo (out, 1));
        return file;
    }

    struct Part
    {
        juce::String name;
        int channel, pitch;
    };

    /** A MIDI file of these parts, a quarter note each at the start. */
    juce::File writeParts (const juce::File& file, const std::vector<Part>& parts)
    {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (96);

        for (const auto& p : parts)
        {
            juce::MidiMessageSequence part;
            part.addEvent (juce::MidiMessage::textMetaEvent (3, p.name), 0.0);
            part.addEvent (juce::MidiMessage::noteOn (p.channel, p.pitch, (juce::uint8) 100), 0.0);
            part.addEvent (juce::MidiMessage::noteOff (p.channel, p.pitch), 96.0);
            midi.addTrack (part);
        }

        juce::FileOutputStream out (file);
        REQUIRE (out.openedOk());
        REQUIRE (midi.writeTo (out, 1));
        return file;
    }

    struct ImportFixture
    {
        ImportFixture()
        {
            model.setSnap (core::snap::Mode::bar);
            project->clearUndoHistory();
        }

        int numNotes (te::Clip* clip) const
        {
            auto* midi = dynamic_cast<te::MidiClip*> (clip);
            return midi != nullptr ? midi->getSequence().getNotes().size() : -1;
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        ArrangementModel model { *project };
    };
}

TEST_SUITE ("arrangement: MIDI files")
{
    TEST_CASE ("one part onto the instrument track it was dropped on; an empty song takes the file's tempo")
    {
        ImportFixture f;
        const auto file = writeFile (f.temp.getFile ("Melody.mid"), 1);
        CHECK (f.model.isSongEmpty());

        CHECK (f.model.importMidi ({ file }, 0, 5.0).isEmpty());   // snapped down to bar 2 (3/4: beat 3)
        dispatch();

        CHECK (f.project->getTempoBpm() == doctest::Approx (95.0).epsilon (0.001));
        CHECK (f.project->getBeatsPerBar() == 3);
        REQUIRE (f.model.getNumTracks() == 1);

        const auto onTrack = clips::getClips (*f.model.getTrack (0));
        REQUIRE (onTrack.size() == 1);
        auto* clip = onTrack.getFirst();
        CHECK (clip->getName() == "Melody");
        CHECK (f.numNotes (clip) == 2);
        CHECK (clips::getEndBeat (*clip) - clips::getStartBeat (*clip) == doctest::Approx (6.0));   // two 3/4 bars
        CHECK (f.model.getOpenClip() == clip);
        CHECK (f.model.isSelected (*clip));
        CHECK_FALSE (f.model.isSongEmpty());

        // One undo step takes it all back, the tempo too.
        f.project->undo();
        dispatch();
        CHECK (clips::getClips (*f.model.getTrack (0)).isEmpty());
        CHECK (f.project->getTempoBpm() == doctest::Approx (120.0));
        CHECK (f.project->getBeatsPerBar() == 4);
    }

    TEST_CASE ("several parts go onto new instrument tracks named after them; a song with notes keeps its tempo")
    {
        ImportFixture f;
        auto* existing = f.project->addMidiClip (*f.project->getTracks().getFirst(), 0.0, 4.0, "Mine");
        core::Note note;
        core::NoteEditor (*existing).add (note);
        f.project->clearUndoHistory();

        CHECK (f.model.importMidi ({ writeFile (f.temp.getFile ("Band.mid"), 3) }, 0, 8.0).isEmpty());
        dispatch();

        CHECK (f.project->getTempoBpm() == doctest::Approx (120.0));
        REQUIRE (f.model.getNumTracks() == 4);
        CHECK (f.model.getTrack (1)->getName() == "Part A");
        CHECK (f.model.getTrack (3)->getName() == "Part C");

        for (int i = 1; i <= 3; ++i)
        {
            const auto onTrack = clips::getClips (*f.model.getTrack (i));
            REQUIRE (onTrack.size() == 1);
            CHECK (clips::getStartBeat (*onTrack[0]) == doctest::Approx (8.0));
            CHECK (f.numNotes (onTrack[0]) == 2);
            CHECK_FALSE (core::tracks::isAudio (*f.model.getTrack (i)));
        }

        CHECK (f.model.getSelectedClips().size() == 3);
        CHECK (f.model.getPlayMode() == PlayMode::song);

        f.project->undo();
        dispatch();
        CHECK (f.model.getNumTracks() == 1);
    }

    TEST_CASE ("a drum part (MIDI channel 10) gets the built-in drums; dropped alone on a new track, that track does")
    {
        ImportFixture f;
        auto* first = f.project->getTracks().getFirst();
        core::NoteEditor (*f.project->addMidiClip (*first, 0.0, 4.0, "Mine")).add (core::Note {});
        f.project->clearUndoHistory();

        const auto song = writeParts (f.temp.getFile ("Song.mid"), { { "Kit", 10, 36 }, { "Piano", 1, 60 } });
        CHECK (f.model.importMidi ({ song }, 0, 0.0).isEmpty());
        dispatch();

        REQUIRE (f.model.getNumTracks() == 3);
        auto* kit = f.model.getTrack (1);
        auto* piano = f.model.getTrack (2);
        CHECK (kit->getName() == "Kit");
        CHECK (audio::instruments::playsDrumKit (*kit));
        CHECK (core::drums::showsNames (*kit));
        CHECK (dynamic_cast<te::FourOscPlugin*> (audio::instruments::get (*piano)) != nullptr);
        CHECK_FALSE (core::drums::showsNames (*piano));

        // Just drums, dropped on a new track: it becomes the drum track. On the piano: it stays a piano.
        const auto groove = writeParts (f.temp.getFile ("Groove.mid"), { { "Groove", 10, 38 } });
        auto* fresh = f.model.addTrack();
        REQUIRE (fresh != nullptr);
        CHECK (f.model.importMidi ({ groove }, 3, 16.0).isEmpty());
        CHECK (audio::instruments::playsDrumKit (*fresh));
        CHECK (fresh->getName() == "Drums");

        CHECK (f.model.importMidi ({ groove }, 2, 32.0).isEmpty());
        CHECK_FALSE (audio::instruments::playsDrumKit (*piano));
        CHECK (f.model.getNumTracks() == 4);
    }

    TEST_CASE ("dropped on an audio track, MIDI goes onto a new track; audio and MIDI files can come together")
    {
        ImportFixture f;
        auto* audioTrack = f.model.addAudioTrack();
        REQUIRE (audioTrack != nullptr);
        dispatch();

        const auto tone = test::writeTone (f.temp.getFile ("Tone.wav"), 1.0);
        const auto midi = writeFile (f.temp.getFile ("Hook.mid"), 1);
        const auto junk = f.temp.getFile ("Broken.mid");
        junk.replaceWithText ("garbage");

        const auto problems = f.model.importFiles ({ tone, midi, junk }, 1, 0.0);
        dispatch();
        CHECK (problems.contains ("Broken.mid"));

        CHECK (clips::getClips (*audioTrack).size() == 1);          // the sound file
        REQUIRE (f.model.getNumTracks() == 3);
        CHECK (f.model.getTrack (2)->getName() == "Hook");
        CHECK (f.numNotes (clips::getClips (*f.model.getTrack (2))[0]) == 2);
    }
}
