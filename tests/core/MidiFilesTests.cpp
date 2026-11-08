#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/Clips.h"
#include "core/MidiFiles.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace midi_files = core::midi_files;

namespace
{
    core::Note makeNote (int pitch, double start, double length, int velocity = 100, int colour = 0)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        n.velocity = velocity;
        n.colour = colour;
        return n;
    }

    /** A file as other programs write it: a conductor track (tempo, 3/4), then a piano and a bass. */
    juce::File writeTwoPartFile (const juce::File& file, short ppq = 480)
    {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ppq);

        juce::MidiMessageSequence conductor;
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent (600000), 0.0);       // 100 BPM
        conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (3, 4), 0.0);
        midi.addTrack (conductor);

        juce::MidiMessageSequence piano;
        piano.addEvent (juce::MidiMessage::textMetaEvent (3, "Piano"), 0.0);
        piano.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), 0.0);
        piano.addEvent (juce::MidiMessage::noteOff (1, 60), ppq * 1.0);
        piano.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 80), ppq * 1.5);
        piano.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 0), ppq * 2.0);    // velocity 0 = off
        midi.addTrack (piano);

        juce::MidiMessageSequence bass;
        bass.addEvent (juce::MidiMessage::textMetaEvent (3, "Bass"), 0.0);
        bass.addEvent (juce::MidiMessage::noteOn (2, 36, (juce::uint8) 110), ppq * 4.0);
        bass.addEvent (juce::MidiMessage::noteOff (2, 36), ppq * 6.5);
        midi.addTrack (bass);

        juce::FileOutputStream out (file);
        REQUIRE (out.openedOk());
        REQUIRE (midi.writeTo (out, 1));
        return file;
    }
}

TEST_SUITE ("core::midi_files")
{
    TEST_CASE ("a file from another program: its parts, notes in beats, tempo and time signature")
    {
        test::TempFolder temp;
        juce::String error;
        const auto contents = midi_files::read (writeTwoPartFile (temp.getFile ("Song.mid")), error);
        REQUIRE_MESSAGE (contents.has_value(), error.toStdString());

        REQUIRE (contents->parts.size() == 2);   // the conductor track has no notes
        CHECK (contents->bpm.value() == doctest::Approx (100.0));
        CHECK (contents->beatsPerBar.value() == 3);

        const auto& piano = contents->parts[0];
        CHECK (piano.name == "Piano");
        REQUIRE (piano.notes.size() == 2);
        CHECK (piano.notes[0].pitch == 60);
        CHECK (piano.notes[0].startBeat == doctest::Approx (0.0));
        CHECK (piano.notes[0].lengthBeats == doctest::Approx (1.0));
        CHECK (piano.notes[0].velocity == 90);
        CHECK (piano.notes[1].startBeat == doctest::Approx (1.5));
        CHECK (piano.notes[1].lengthBeats == doctest::Approx (0.5));
        CHECK (piano.endBeat == doctest::Approx (2.0));

        const auto& bass = contents->parts[1];
        CHECK (bass.name == "Bass");
        REQUIRE (bass.notes.size() == 1);
        CHECK (bass.notes[0].colour == 1);     // channel 2
        CHECK (bass.notes[0].startBeat == doctest::Approx (4.0));
        CHECK (bass.notes[0].lengthBeats == doctest::Approx (2.5));
        CHECK (bass.endBeat == doctest::Approx (6.5));
    }

    TEST_CASE ("files that can't be used say why")
    {
        test::TempFolder temp;
        juce::String error;

        const auto text = temp.getFile ("Notes.mid");
        text.replaceWithText ("not a MIDI file");
        CHECK_FALSE (midi_files::read (text, error).has_value());
        CHECK (error.contains ("Can't read"));

        juce::MidiFile empty;
        empty.setTicksPerQuarterNote (96);
        juce::MidiMessageSequence onlyTempo;
        onlyTempo.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
        empty.addTrack (onlyTempo);
        const auto emptyFile = temp.getFile ("Empty.mid");
        {
            juce::FileOutputStream out (emptyFile);
            empty.writeTo (out);
        }
        CHECK_FALSE (midi_files::read (emptyFile, error).has_value());
        CHECK (error.contains ("no notes"));

        juce::MidiFile smpte;
        smpte.setSmpteTimeFormat (25, 40);
        juce::MidiMessageSequence notes;
        notes.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
        notes.addEvent (juce::MidiMessage::noteOff (1, 60), 100.0);
        smpte.addTrack (notes);
        const auto smpteFile = temp.getFile ("Film.mid");
        {
            juce::FileOutputStream out (smpteFile);
            smpte.writeTo (out);
        }
        CHECK_FALSE (midi_files::read (smpteFile, error).has_value());
        CHECK (error.contains ("SMPTE"));

        CHECK (midi_files::isMidiFile (juce::File ("C:/a/b.MID")));
        CHECK (midi_files::isMidiFile (juce::File ("C:/a/b.midi")));
        CHECK_FALSE (midi_files::isMidiFile (juce::File ("C:/a/b.wav")));
    }

    TEST_CASE ("a clip written and read back: what it plays, from its start")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->setTempoBpm (90.0);
        auto& track = *project->getTracks().getFirst();
        auto* clip = project->addMidiClip (track, 8.0, 8.0, "Riff");
        REQUIRE (clip != nullptr);

        auto muted = makeNote (70, 3.0, 1.0);
        muted.muted = true;
        core::NoteEditor (*clip).add ({ makeNote (60, 0.0, 1.0, 100), makeNote (62, 1.5, 0.25, 64, 3), muted,
                                        makeNote (67, 7.0, 4.0) });   // runs past the clip's end

        const auto file = temp.getFile ("Riff.mid");
        REQUIRE (midi_files::writeClip (*clip, file).isEmpty());

        juce::String error;
        const auto contents = midi_files::read (file, error);
        REQUIRE (contents.has_value());
        REQUIRE (contents->parts.size() == 1);
        CHECK (contents->parts[0].name == "Riff");
        CHECK (contents->bpm.value() == doctest::Approx (90.0).epsilon (0.001));
        CHECK (contents->beatsPerBar.value() == 4);

        const auto& notes = contents->parts[0].notes;
        REQUIRE (notes.size() == 3);                       // not the muted one
        CHECK (notes[0] == makeNote (60, 0.0, 1.0, 100));
        CHECK (notes[1] == makeNote (62, 1.5, 0.25, 64, 3));
        CHECK (notes[2].startBeat == doctest::Approx (7.0));
        CHECK (notes[2].lengthBeats == doctest::Approx (1.0));   // cut where the clip ends

        // A clip without notes has nothing to export.
        auto* emptyClip = project->addMidiClip (track, 20.0, 4.0, "Empty");
        CHECK (midi_files::writeClip (*emptyClip, temp.getFile ("Empty.mid")).startsWith ("Nothing to export"));
    }

    TEST_CASE ("the song: a track per instrument track with notes where they play")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->setBeatsPerBar (3);
        auto& lead = *project->getTracks().getFirst();
        lead.setName ("Lead");
        core::NoteEditor (*project->addMidiClip (lead, 0.0, 4.0, "A")).add ({ makeNote (72, 1.0, 1.0) });
        core::NoteEditor (*project->addMidiClip (lead, 12.0, 4.0, "B")).add ({ makeNote (74, 0.0, 2.0) });

        auto* drums = project->addTrack ("Drums");
        core::NoteEditor (*project->addMidiClip (*drums, 4.0, 4.0, "Beat")).add ({ makeNote (36, 0.0, 0.5, 120, 9) });

        project->addTrack ("Empty");
        auto* audio = project->addTrack ("Vocal");
        core::tracks::setKind (*audio, core::tracks::Kind::audio);

        const auto file = temp.getFile ("Song.mid");
        REQUIRE (midi_files::writeSong (*project, file).isEmpty());

        juce::String error;
        const auto contents = midi_files::read (file, error);
        REQUIRE (contents.has_value());
        CHECK (contents->beatsPerBar.value() == 3);
        REQUIRE (contents->parts.size() == 2);
        CHECK (contents->parts[0].name == "Lead");
        REQUIRE (contents->parts[0].notes.size() == 2);
        CHECK (contents->parts[0].notes[0].startBeat == doctest::Approx (1.0));
        CHECK (contents->parts[0].notes[1].startBeat == doctest::Approx (12.0));
        CHECK (contents->parts[1].name == "Drums");
        CHECK (contents->parts[1].notes[0].startBeat == doctest::Approx (4.0));
        CHECK (contents->parts[1].notes[0].colour == 9);   // channel 10

        // Nothing to write without notes.
        auto empty = core::Project::createEmpty (test::session());
        CHECK (midi_files::writeSong (*empty, temp.getFile ("Nothing.mid")).startsWith ("Nothing to export"));
    }
}
