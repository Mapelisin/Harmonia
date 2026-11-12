#include <doctest.h>

#include "core/TestProject.h"
#include "audio/Freeze.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
#include "core/Controllers.h"
#include "core/NoteEditor.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/NoteInput.h"

using namespace harmonia;
using namespace harmonia::ui;
using core::InputQuantize;

namespace
{
    /** A song position the test sets by hand: "the listener hears beat X now". */
    struct FakeClock final : audio::SongClock
    {
        bool isPlaying() const override                             { return playing; }
        std::optional<double> getHeardBeat (double) const override  { return playing ? std::optional<double> (beat) : std::nullopt; }
        juce::Range<double> getLoopBeats() const override           { return loop; }

        bool playing = false;
        double beat = 0.0;
        juce::Range<double> loop { 0.0, 16.0 };     // clip mode: the fixture's clip
    };

    /** Keys held down on a pretend keyboard, by what they type on a US layout. */
    struct FakeKeys
    {
        std::set<juce::juce_wchar> down;

        ComputerKeyboard::KeyReader reader()
        {
            return [this] (const ComputerKeyboard::Key& key) { return down.count (key.character) > 0; };
        }
    };

    struct NoteInputFixture : test::ProjectWithClip
    {
        NoteInputFixture()
        {
            model.setClip (clip);
            model.setSnap (core::snap::Mode::quarterBeat);
        }

        ~NoteInputFixture()
        {
            input.reset();
        }

        std::vector<core::Note> notes() const      { return test::readNotes (*clip); }

        void play (int pitch, double from, double to, int velocity = 100)
        {
            clock.beat = from;
            input->noteOn (pitch, velocity, NoteInput::Source::midiKeyboard);
            clock.beat = to;
            input->noteOff (pitch, NoteInput::Source::midiKeyboard);
        }

        pianoroll::PianoRollModel model { *project };
        audio::NotePreview preview;
        FakeClock clock;
        std::unique_ptr<NoteInput> input = std::make_unique<NoteInput> (model, preview, clock);
    };

    juce::KeyPress key (int code, juce::juce_wchar text = 0, int mods = 0)
    {
        return juce::KeyPress (code, juce::ModifierKeys (mods), text);
    }
}

TEST_SUITE ("note input: keys")
{
    TEST_CASE ("shortcut letters are the same keys with the Russian layout")
    {
        // What JUCE reports on Windows: the Latin letter as the key code, the Cyrillic one as text.
        CHECK (keys::getLetter (key ('P', 0x0437)) == 'P');                 // З
        // What other systems report: the typed Cyrillic letter.
        CHECK (keys::getLetter (key (0x0437)) == 'P');                      // з
        CHECK (keys::getLetter (key (0x0424)) == 'A');                      // Ф
        CHECK (keys::getLetter (key (0x044f)) == 'Z');                      // я
        CHECK (keys::getLetter (key ('b')) == 'B');
        CHECK (keys::getLetter (key ('5')) == 0);

        CHECK (keys::isTypingKey (key ('z')));
        CHECK (keys::isTypingKey (key (0x0436)));                           // ж
        CHECK (keys::isTypingKey (key ('q', 0, juce::ModifierKeys::shiftModifier)));
        CHECK_FALSE (keys::isTypingKey (key ('z', 0, juce::ModifierKeys::ctrlModifier)));
        CHECK_FALSE (keys::isTypingKey (key (juce::KeyPress::spaceKey)));
        CHECK_FALSE (keys::isTypingKey (key (juce::KeyPress::upKey)));
        CHECK_FALSE (keys::isTypingKey (key (juce::KeyPress::deleteKey)));
        CHECK_FALSE (keys::isTypingKey (key (juce::KeyPress::escapeKey)));
    }

    TEST_CASE ("the computer keyboard plays two rows like piano keys, chords included")
    {
        FakeKeys keys;
        ComputerKeyboard keyboard (keys.reader());
        std::vector<int> on, off;
        keyboard.onNoteOn = [&] (int pitch, int) { on.push_back (pitch); };
        keyboard.onNoteOff = [&] (int pitch) { off.push_back (pitch); };

        // Off: nothing plays.
        keys.down = { 'z' };
        keyboard.update (true);
        CHECK (on.empty());

        keyboard.setEnabled (true);
        keyboard.update (true);
        CHECK (on == std::vector<int> { 60 });           // Z = C4

        keys.down = { 'z', 'c', 'b', 'q', '2' };          // C E G, and C5 C#5 on the upper row
        keyboard.update (true);
        CHECK (on == std::vector<int> { 60, 64, 67, 72, 73 });

        // Auto-repeat: polling again doesn't retrigger anything.
        keyboard.update (true);
        CHECK (on.size() == 5);

        keys.down = { 'q' };
        keyboard.update (true);
        CHECK (off.size() == 4);
        CHECK (keyboard.isPlayingAnything());

        keys.down.clear();
        keyboard.update (true);
        CHECK (off.size() == 5);
        CHECK_FALSE (keyboard.isPlayingAnything());
    }

    TEST_CASE ("two keys on the same pitch, octave changes and Ctrl held")
    {
        FakeKeys keys;
        ComputerKeyboard keyboard (keys.reader());
        std::vector<int> on, off;
        keyboard.onNoteOn = [&] (int pitch, int) { on.push_back (pitch); };
        keyboard.onNoteOff = [&] (int pitch) { off.push_back (pitch); };
        keyboard.setEnabled (true);

        // ',' (lower row, C5) and 'Q' (upper row, C5) share a pitch: one note.
        keys.down = { ',', 'q' };
        keyboard.update (true);
        CHECK (on == std::vector<int> { 72 });
        keys.down = { 'q' };
        keyboard.update (true);
        CHECK (off.empty());
        keys.down.clear();
        keyboard.update (true);
        CHECK (off == std::vector<int> { 72 });

        // A new octave stops what is held and plays from there.
        keys.down = { 'z' };
        keyboard.update (true);
        keyboard.setOctave (2);
        CHECK (off.back() == 60);
        keyboard.update (true);
        CHECK (on.back() == 36);

        keyboard.setOctave (100);
        CHECK (keyboard.getOctave() == ComputerKeyboard::maxOctave);

        // Ctrl held (Ctrl+Z is undo): no new notes, but releasing still works.
        keyboard.releaseAll();
        on.clear();
        keys.down = { 'x' };
        keyboard.update (false);
        CHECK (on.empty());

        keyboard.setEnabled (false);
        CHECK_FALSE (keyboard.isPlayingAnything());
    }
}

TEST_SUITE ("note input: recording")
{
    TEST_CASE ("notes are recorded only while armed and playing, at the position heard")
    {
        NoteInputFixture f;

        // Playing but not armed: heard, not recorded.
        f.clock.playing = true;
        f.play (60, 1.0, 2.0);
        CHECK (f.notes().empty());

        // Armed but stopped: nothing either.
        f.clock.playing = false;
        f.input->setRecordArmed (true);
        f.play (60, 1.0, 2.0);
        CHECK (f.notes().empty());
        CHECK_FALSE (f.input->isRecording());

        f.clock.playing = true;
        f.input->update();
        CHECK (f.input->isRecording());

        f.play (62, 2.5, 3.25, 90);
        f.play (65, 4.125, 4.5);

        const auto n = f.notes();
        REQUIRE (n.size() == 2);
        CHECK (n[0].pitch == 62);
        CHECK (n[0].velocity == 90);
        CHECK (n[0].startBeat == doctest::Approx (2.5));
        CHECK (n[0].lengthBeats == doctest::Approx (0.75));
        CHECK (n[1].startBeat == doctest::Approx (4.125));
    }

    TEST_CASE ("a whole take is one undo step, held keys show before they become notes")
    {
        NoteInputFixture f;
        f.input->setRecordArmed (true);
        f.clock.playing = true;
        f.input->update();

        f.play (60, 0.0, 1.0);
        f.play (62, 1.0, 2.0);

        f.clock.beat = 3.0;
        f.input->noteOn (64, 100, NoteInput::Source::midiKeyboard);
        f.clock.beat = 3.5;
        f.input->update();
        REQUIRE (f.model.getLiveNotes().size() == 1);
        CHECK (f.model.getLiveNotes()[0].lengthBeats == doctest::Approx (0.5));
        CHECK (f.model.isPitchHeld (64));
        CHECK (f.notes().size() == 2);

        // Stop: the held key's note ends where playback stopped.
        f.clock.beat = 4.0;
        f.input->update();
        f.clock.playing = false;
        f.input->update();
        CHECK_FALSE (f.input->isRecording());
        CHECK (f.model.getLiveNotes().empty());
        REQUIRE (f.notes().size() == 3);
        CHECK (f.notes()[2].getEndBeat() == doctest::Approx (4.0));

        f.input->noteOff (64, NoteInput::Source::midiKeyboard);
        CHECK_FALSE (f.model.isPitchHeld (64));

        f.project->undo();
        CHECK (f.notes().empty());
    }

    TEST_CASE ("looping: a note over the loop end stops there, the next pass adds without stacking copies")
    {
        NoteInputFixture f;   // the clip loops beats 0..16
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        f.play (60, 15.5, 0.5);
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].startBeat == doctest::Approx (15.5));
        CHECK (f.notes()[0].getEndBeat() == doctest::Approx (16.0));

        // The same note on the next pass is already there; a new one is added.
        f.play (60, 15.5, 0.5);
        f.play (67, 2.0, 2.5);
        CHECK (f.notes().size() == 2);
    }

    TEST_CASE ("quantize puts recorded notes on the piano roll's grid")
    {
        NoteInputFixture f;
        f.input->setRecordArmed (true);
        f.input->setQuantize (InputQuantize::startsAndEnds);
        f.clock.playing = true;

        f.play (60, 1.07, 1.4);     // grid: quarter beats
        f.play (62, 15.94, 0.2);    // just before the loop's first beat

        const auto n = f.notes();
        REQUIRE (n.size() == 2);
        CHECK (n[0].startBeat == doctest::Approx (0.0));
        CHECK (n[1].startBeat == doctest::Approx (1.0));
        CHECK (n[1].getEndBeat() == doctest::Approx (1.5));
    }

    TEST_CASE ("the sustain pedal of a MIDI keyboard lengthens recorded notes")
    {
        NoteInputFixture f;
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        const auto t = NoteInput::now();
        f.clock.beat = 1.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 64, 127), t);
        f.input->handleMidiMessage (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), t);
        f.clock.beat = 1.5;
        f.input->handleMidiMessage (juce::MidiMessage::noteOff (1, 60), t);
        CHECK (f.notes().empty());

        f.clock.beat = 3.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 64, 0), t);
        REQUIRE (f.notes().size() == 1);
        CHECK (f.notes()[0].getEndBeat() == doctest::Approx (3.0));
        CHECK (f.notes()[0].velocity == 100);
    }

    TEST_CASE ("switching the take's clip away finishes it into the original clip")
    {
        NoteInputFixture f;
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        f.clock.beat = 1.0;
        f.input->noteOn (60, 100, NoteInput::Source::midiKeyboard);
        f.clock.beat = 2.0;
        f.input->update();
        f.input->finishTake();

        CHECK (f.notes().size() == 1);
        CHECK (f.notes()[0].getEndBeat() == doctest::Approx (2.0));
    }

    TEST_CASE ("in song mode what is played inside the open clip goes in, cut at its end; the rest gets a clip of its own")
    {
        NoteInputFixture f;
        f.clock.loop = { 0.0, 32.0 };     // the song loops, the clip is beats 0..16
        f.project->clearUndoHistory();

        juce::Array<te::MidiClip*> made;
        f.input->onClipsMade = [&] (const juce::Array<te::MidiClip*>& clips) { made = clips; };
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        f.play (60, 2.0, 3.0);
        f.play (62, 20.0, 21.0);
        f.play (64, 15.0, 18.0);

        // Past the clip: a clip of the take's own, there at once (the song's next time round plays it).
        auto clips = core::clips::getMidiClips (*f.track);
        REQUIRE (clips.size() == 2);
        CHECK (clips[0] == f.clip);
        CHECK (core::clips::getStartBeat (*clips[1]) == doctest::Approx (20.0));
        CHECK (core::clips::getEndBeat (*clips[1]) == doctest::Approx (24.0));
        CHECK (made.isEmpty());             // said when the take ends

        // A key held on into the song's next pass shows as being recorded only inside the clip.
        f.clock.beat = 30.0;
        f.input->noteOn (65, 100, NoteInput::Source::midiKeyboard);
        f.input->update();
        CHECK (f.model.getLiveNotes().empty());

        f.clock.playing = false;
        f.input->update();

        const auto notes = f.notes();
        REQUIRE (notes.size() == 2);
        CHECK (notes[0].pitch == 60);
        CHECK (notes[1].pitch == 64);
        CHECK (notes[1].getEndBeat() == doctest::Approx (16.0));
        CHECK (core::clips::getEndBeat (*f.clip) == doctest::Approx (16.0));

        // The take's clip has grown to hold the key that was still down at the stop.
        REQUIRE (made.size() == 1);
        CHECK (made[0] == clips[1]);
        CHECK (core::clips::getStartBeat (*made[0]) == doctest::Approx (20.0));
        CHECK (core::clips::getEndBeat (*made[0]) == doctest::Approx (32.0));

        const auto elsewhere = test::readNotes (*made[0]);
        REQUIRE (elsewhere.size() == 2);
        CHECK (elsewhere[0].pitch == 62);
        CHECK (elsewhere[0].startBeat == doctest::Approx (0.0));
        CHECK (elsewhere[0].lengthBeats == doctest::Approx (1.0));
        CHECK (elsewhere[1].pitch == 65);
        CHECK (elsewhere[1].startBeat == doctest::Approx (10.0));

        // The whole take, its clip too, is one step back.
        f.project->getEdit().dispatchPendingUpdatesSynchronously();
        f.project->undo();
        CHECK (f.notes().empty());
        CHECK (core::clips::getMidiClips (*f.track).size() == 1);
        CHECK_FALSE (f.project->canUndo());
    }

    TEST_CASE ("with no clip open, what is played is recorded on the current track: a clip is made for it")
    {
        NoteInputFixture f;
        f.model.setClip (nullptr);
        f.clock.loop = {};                  // the song plays straight on
        auto* second = f.project->addTrack ("Second");
        REQUIRE (second != nullptr);
        f.project->clearUndoHistory();

        juce::Array<te::MidiClip*> made;
        int reports = 0;
        f.input->onClipsMade = [&] (const juce::Array<te::MidiClip*>& clips) { made = clips; ++reports; };
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        // No track is played: there is nowhere to record.
        f.input->update();
        CHECK_FALSE (f.input->isRecording());
        f.play (60, 1.0, 2.0);
        CHECK (f.notes().empty());

        f.input->setTrack (second);
        f.input->update();
        CHECK (f.input->isRecording());

        f.play (60, 33.0, 34.0);
        f.play (64, 34.0, 37.5, 80);

        // The mod wheel, moved after the notes.
        f.clock.beat = 38.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 1, 127), NoteInput::now());

        auto clips = core::clips::getMidiClips (*second);
        REQUIRE (clips.size() == 1);
        CHECK (core::clips::getStartBeat (*clips[0]) == doctest::Approx (32.0));
        CHECK (core::clips::getEndBeat (*clips[0]) == doctest::Approx (40.0));
        CHECK (reports == 0);

        f.clock.playing = false;
        f.input->update();
        CHECK_FALSE (f.input->isRecording());
        CHECK (reports == 1);
        CHECK (made == clips);

        const auto notes = test::readNotes (*clips[0]);
        REQUIRE (notes.size() == 2);
        CHECK (notes[0].pitch == 60);
        CHECK (notes[0].startBeat == doctest::Approx (1.0));
        CHECK (notes[1].pitch == 64);
        CHECK (notes[1].velocity == 80);
        CHECK (notes[1].startBeat == doctest::Approx (2.0));
        CHECK (notes[1].lengthBeats == doctest::Approx (3.5));

        const auto wheel = core::controllers::getPlayed (*clips[0], core::controllers::Controller::modWheel);
        REQUIRE (wheel.size() == 1);
        CHECK (wheel[0].beat == doctest::Approx (38.0));
        CHECK (wheel[0].value == doctest::Approx (1.0f));

        // Nothing went to the other track's clip.
        CHECK (f.notes().empty());

        // A take that made nothing says nothing.
        f.clock.playing = true;
        f.play (67, 35.0, 36.0);            // inside the clip that is there now
        f.clock.playing = false;
        f.input->update();
        CHECK (reports == 1);
        CHECK (test::readNotes (*clips[0]).size() == 3);
        f.project->getEdit().dispatchPendingUpdatesSynchronously();
        f.project->undo();

        // One step back takes the first take's notes and its clip.
        f.project->undo();
        CHECK (core::clips::getMidiClips (*second).isEmpty());
        CHECK_FALSE (f.project->canUndo());
    }

    TEST_CASE ("quantize puts what is played outside the open clip on the grid too")
    {
        NoteInputFixture f;
        f.clock.loop = {};
        f.input->setQuantize (InputQuantize::starts);
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        // A little early for beat 16, where the open clip ends: the note belongs after it.
        f.play (60, 15.95, 16.9);
        CHECK (f.notes().empty());

        const auto clips = core::clips::getMidiClips (*f.track);
        REQUIRE (clips.size() == 2);
        CHECK (core::clips::getStartBeat (*clips[1]) == doctest::Approx (16.0));
        CHECK (core::clips::getEndBeat (*clips[1]) == doctest::Approx (20.0));

        const auto notes = test::readNotes (*clips[1]);
        REQUIRE (notes.size() == 1);
        CHECK (notes[0].startBeat == doctest::Approx (0.0));
    }

    TEST_CASE ("a punch keeps only what is played inside it; a note played a moment early is its first note")
    {
        NoteInputFixture f;
        f.clock.loop = {};      // a punch plays straight on
        f.project->clearUndoHistory();

        std::optional<juce::Range<double>> punch = juce::Range<double> (4.0, 8.0);
        f.input->punchBeats = [&punch] { return punch; };
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        f.play (60, 1.0, 2.0);          // on the way in
        f.play (62, 3.95, 4.5);         // a moment before the punch: its first note
        f.play (64, 5.0, 6.0);
        f.play (65, 7.5, 9.0);          // held past the end: stops there
        f.play (67, 9.0, 10.0);         // after it
        f.play (69, 3.0, 5.0);          // begun well before it: not the punch's

        // The mod wheel, moved before the punch and inside it.
        f.clock.beat = 2.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 1, 100), NoteInput::now());
        f.clock.beat = 6.0;
        f.input->handleMidiMessage (juce::MidiMessage::controllerEvent (1, 1, 50), NoteInput::now());

        // A key held outside the punch doesn't show as being recorded; inside it does.
        f.clock.beat = 9.5;
        f.input->noteOn (71, 100, NoteInput::Source::midiKeyboard);
        f.clock.beat = 9.75;
        f.input->update();
        CHECK (f.model.getLiveNotes().empty());
        f.input->noteOff (71, NoteInput::Source::midiKeyboard);

        f.clock.beat = 6.5;
        f.input->noteOn (72, 100, NoteInput::Source::midiKeyboard);
        f.clock.beat = 7.0;
        f.input->update();
        CHECK (f.model.getLiveNotes().size() == 1);
        f.input->noteOff (72, NoteInput::Source::midiKeyboard);

        f.clock.playing = false;
        f.input->update();

        const auto notes = f.notes();
        REQUIRE (notes.size() == 4);
        CHECK (notes[0].pitch == 62);
        CHECK (notes[0].startBeat == doctest::Approx (4.0));
        CHECK (notes[0].lengthBeats == doctest::Approx (0.5));
        CHECK (notes[1].pitch == 64);
        CHECK (notes[1].startBeat == doctest::Approx (5.0));
        CHECK (notes[2].pitch == 72);
        CHECK (notes[3].pitch == 65);
        CHECK (notes[3].startBeat == doctest::Approx (7.5));
        CHECK (notes[3].lengthBeats == doctest::Approx (0.5));

        const auto wheel = core::controllers::getPlayed (*f.clip, core::controllers::Controller::modWheel);
        REQUIRE (wheel.size() == 1);
        CHECK (wheel[0].beat == doctest::Approx (6.0));

        // Punch off: everything played is recorded again.
        punch.reset();
        f.clock.playing = true;
        f.play (60, 1.0, 2.0);
        f.clock.playing = false;
        f.input->update();
        CHECK (f.notes().size() == 5);
    }

    TEST_CASE ("nothing is recorded where notes can't go: an audio track, a frozen track")
    {
        NoteInputFixture f;
        f.model.setClip (nullptr);
        f.clock.loop = {};
        f.input->setRecordArmed (true);
        f.clock.playing = true;

        auto* guitar = f.project->addTrack ("Guitar");
        REQUIRE (guitar != nullptr);
        core::tracks::setKind (*guitar, core::tracks::Kind::audio);
        f.input->setTrack (guitar);
        f.input->update();
        CHECK_FALSE (f.input->isRecording());
        f.play (60, 1.0, 2.0);
        CHECK (guitar->getClips().isEmpty());

        // A frozen track plays a file of its sound: nothing new goes on it.
        REQUIRE (audio::instruments::useBuiltInSynth (*f.track).isEmpty());
        core::Note held;
        held.lengthBeats = 4.0;
        core::NoteEditor (*f.clip).add (held);
        REQUIRE (audio::freeze::freeze (*f.project, *f.track).isEmpty());
        REQUIRE (audio::freeze::isFrozen (*f.track));

        f.input->setTrack (f.track);
        f.input->update();
        CHECK_FALSE (f.input->isRecording());
        f.play (62, 20.0, 21.0);
        CHECK (core::clips::getMidiClips (*f.track).size() == 1);

        // Unfrozen, it takes notes again.
        audio::freeze::unfreeze (*f.project, *f.track);
        f.input->update();
        CHECK (f.input->isRecording());
        f.play (62, 20.0, 21.0);
        CHECK (core::clips::getMidiClips (*f.track).size() == 2);
    }
}

TEST_SUITE ("note input: main window")
{
    TEST_CASE ("Ctrl+T, R and the computer keyboard taking over the letter shortcuts")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto& model = main.getPianoRollModel();
        auto& input = main.getNoteInput();
        constexpr int ctrl = juce::ModifierKeys::ctrlModifier;

        // A tool letter with the Russian layout (И is on the B key).
        CHECK (main.keyPressed (key ('B', 0x0438)));
        CHECK (model.getTool() == pianoroll::Tool::paint);

        CHECK (main.keyPressed (key ('t', 0, ctrl)));
        CHECK (input.getComputerKeyboard().isEnabled());

        // Now letters are piano keys: no tool change.
        CHECK (main.keyPressed (key ('p')));
        CHECK (model.getTool() == pianoroll::Tool::paint);

        // Up / Down: octave.
        const auto octave = input.getComputerKeyboard().getOctave();
        CHECK (main.keyPressed (key (juce::KeyPress::upKey)));
        CHECK (input.getComputerKeyboard().getOctave() == octave + 1);

        // R is a piano key while the keyboard plays: recording stays as it is.
        CHECK (main.keyPressed (key ('r')));
        CHECK_FALSE (input.isRecordArmed());

        // Ctrl shortcuts still work.
        CHECK (main.keyPressed (key ('t', 0, ctrl)));
        CHECK_FALSE (input.getComputerKeyboard().isEnabled());
        CHECK (main.keyPressed (key ('p')));
        CHECK (model.getTool() == pianoroll::Tool::draw);

        // Plain R toggles recording when the keyboard isn't playing notes (also with the Russian layout: К).
        CHECK (main.keyPressed (key ('R', 0x043a)));
        CHECK (input.isRecordArmed());
        CHECK (main.keyPressed (key ('r')));
        CHECK_FALSE (input.isRecordArmed());

        // L: clip / song (Д with the Russian layout).
        auto& arrangement = main.getArrangementModel();
        CHECK (arrangement.getPlayMode() == arrangement::PlayMode::clip);
        CHECK (main.keyPressed (key ('L', 0x0434)));
        CHECK (arrangement.getPlayMode() == arrangement::PlayMode::song);
        CHECK (main.keyPressed (key ('l')));
        CHECK (arrangement.getPlayMode() == arrangement::PlayMode::clip);

        const auto click = project->getEdit().clickTrackEnabled.get();
        CHECK (main.keyPressed (key ('m', 0, ctrl)));
        CHECK (project->getEdit().clickTrackEnabled.get() != click);
    }

    TEST_CASE ("armed to record, the whole song isn't a loop; a loop region and the open clip still are")
    {
        test::ProjectWithClip song;
        auto& project = song.project;
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto& arrangement = main.getArrangementModel();
        auto& input = main.getNoteInput();
        auto& transport = project->getEdit().getTransport();
        arrangement.openClip (song.clip);
        REQUIRE (arrangement.getOpenClip() == song.clip);

        // Clip mode: the open clip goes round, armed or not.
        REQUIRE (arrangement.getPlayMode() == arrangement::PlayMode::clip);
        CHECK (transport.looping.get());
        input.setRecordArmed (true);
        CHECK (transport.looping.get());
        input.setRecordArmed (false);

        // Song mode: the song goes round, until recording is armed.
        CHECK (main.keyPressed (key ('l')));
        REQUIRE (arrangement.getPlayMode() == arrangement::PlayMode::song);
        CHECK (transport.looping.get());
        input.setRecordArmed (true);
        CHECK_FALSE (transport.looping.get());
        input.setRecordArmed (false);
        CHECK (transport.looping.get());

        // A loop region is a loop to record round.
        arrangement.setLoopRegion ({ 4.0, 12.0 });
        input.setRecordArmed (true);
        CHECK (transport.looping.get());
        input.setRecordArmed (false);
    }

    TEST_CASE ("the Punch button makes the loop region the place recordings go; only in song mode, only with a region")
    {
        test::ProjectWithClip song;
        auto& project = song.project;
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto& arrangement = main.getArrangementModel();
        auto& transport = main.getTransport();
        auto& button = main.getTransportBar().getPunchButton();
        arrangement.openClip (song.clip);
        CHECK (button.isShowing() == main.isShowing());
        CHECK (button.getRight() <= main.getTransportBar().getWidth());
        CHECK_FALSE (arrangement.isPunchOn());
        CHECK_FALSE (transport.isPunchOn());

        // On in clip mode: nothing to punch into yet.
        REQUIRE (arrangement.getPlayMode() == arrangement::PlayMode::clip);
        button.onClick();
        CHECK (arrangement.isPunchOn());
        CHECK_FALSE (arrangement.getPunchRegion().has_value());
        CHECK_FALSE (transport.isPunchOn());
        CHECK (main.getStatusBar().getMessage().startsWith ("Punch is on: set a loop region"));

        // Song mode with a loop region: that is where a recording goes.
        main.setPlayMode (arrangement::PlayMode::song);
        CHECK_FALSE (transport.isPunchOn());
        arrangement.setLoopRegion ({ 8.0, 16.0 });
        main.setPunch (true);
        REQUIRE (arrangement.getPunchRegion().has_value());
        CHECK (arrangement.getPunchRegion()->start == doctest::Approx (8.0));
        CHECK (transport.isPunchOn());
        CHECK (main.getStatusBar().getMessage()
                 == "Punch is on: only bars 3-4 will be recorded; the song starts a bar earlier");

        // The window paints with the region as a punch.
        CHECK (main.createComponentSnapshot (main.getLocalBounds()).isValid());

        // Back in clip mode the open clip is what plays round: no punch.
        main.setPlayMode (arrangement::PlayMode::clip);
        CHECK (arrangement.isPunchOn());
        CHECK_FALSE (transport.isPunchOn());
        main.setPlayMode (arrangement::PlayMode::song);
        CHECK (transport.isPunchOn());

        button.onClick();
        CHECK_FALSE (arrangement.isPunchOn());
        CHECK_FALSE (transport.isPunchOn());
        CHECK (main.getStatusBar().getMessage() == "Punch is off: recordings go on from where the song is");
    }

    TEST_CASE ("a take's new clip opens in the piano roll when no clip was open, and the status bar says where it is")
    {
        test::ProjectWithClip song;
        auto& project = song.project;
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto& arrangement = main.getArrangementModel();
        auto& input = main.getNoteInput();
        auto* first = song.clip;
        arrangement.openClip (first);
        REQUIRE (arrangement.getOpenClip() == first);
        REQUIRE (input.onClipsMade != nullptr);

        // A track with no clip: none is open.
        auto* second = project->addTrack ("Second");
        REQUIRE (second != nullptr);
        arrangement.setCurrentTrack (second);
        REQUIRE (arrangement.getOpenClip() == nullptr);
        REQUIRE (main.getPianoRollModel().getClip() == nullptr);

        // What a take does when notes are played there.
        auto* made = project->addMidiClip (*second, 16.0, 16.0, "Second");
        REQUIRE (made != nullptr);
        input.onClipsMade ({ made });

        CHECK (arrangement.getOpenClip() == made);
        CHECK (main.getPianoRollModel().getClip() == made);
        CHECK (main.getStatusBar().getMessage() == "Recorded into a new clip on Second: bars 5-8. Ctrl+Z takes the whole take back");

        // A clip was open: it stays; two new clips are both named.
        auto* another = project->addMidiClip (*second, 40.0, 4.0, "Second");
        REQUIRE (another != nullptr);
        auto* third = project->addMidiClip (*second, 48.0, 8.0, "Second");
        REQUIRE (third != nullptr);
        input.onClipsMade ({ another, third });
        CHECK (arrangement.getOpenClip() == made);
        CHECK (main.getStatusBar().getMessage() == "Recorded into 2 new clips on Second: bar 11, bars 13-14. Ctrl+Z takes the whole take back");

        // The user has gone to another track since: its clip stays open.
        arrangement.openClip (first);
        auto* onSecond = project->addMidiClip (*second, 64.0, 4.0, "Second");
        REQUIRE (onSecond != nullptr);
        input.onClipsMade ({ onSecond });
        CHECK (arrangement.getOpenClip() == first);
    }
}

TEST_SUITE ("note input: main window")
{
    TEST_CASE ("the main window paints with Punch on: the loop region is where a recording goes")
    {
        test::ProjectWithClip song;
        MainComponent main (test::session(), *song.project);
        main.setBounds (0, 0, 1280, 800);
        main.getArrangementModel().openClip (song.clip);
        main.setPlayMode (arrangement::PlayMode::song);
        main.getArrangementModel().setLoopRegion ({ 8.0, 16.0 });
        main.setPunch (true);
        main.getNoteInput().setRecordArmed (true);

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-punch.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }

    TEST_CASE ("the main window paints while recording, with held keys and notes being recorded")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        auto& model = main.getPianoRollModel();
        model.beginGesture ("notes");

        for (int i = 0; i < 6; ++i)
        {
            core::Note n;
            n.pitch = 60 + (i % 3) * 4;
            n.startBeat = i * 0.5;
            n.lengthBeats = 0.5;
            model.addNote (n);
        }

        core::Note live;
        live.pitch = 67;
        live.startBeat = 3.0;
        live.lengthBeats = 0.75;
        model.setLiveNotes ({ live });

        std::array<bool, 128> held {};
        held[67] = true;
        model.setHeldPitches (held);

        main.getNoteInput().setRecordArmed (true);
        main.getNoteInput().getComputerKeyboard().setEnabled (true);

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-recording.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
