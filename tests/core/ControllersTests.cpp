#include <doctest.h>

#include "TestSession.h"
#include "TestAudioFiles.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/Controllers.h"
#include "core/MidiFiles.h"
#include "core/Project.h"

using namespace harmonia;
namespace controllers = core::controllers;
using controllers::Controller;

namespace
{
    struct ClipFixture
    {
        ClipFixture()
        {
            clip = project->addMidiClip (*project->getTracks().getFirst(), 0.0, 16.0, "Clip");
            REQUIRE (clip != nullptr);
            project->clearUndoHistory();
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::MidiClip* clip = nullptr;
    };

    /** Beat and value match (values are kept as MIDI's steps: 0.25 is 32/127). */
    bool sameAs (const controllers::Point& p, double beat, float value)
    {
        return std::abs (p.beat - beat) < 1.0e-6 && std::abs (p.value - value) < 0.01f;
    }

    /** The MIDI the clip plays, as Tracktion makes it. */
    juce::MidiMessageSequence playedMidi (te::MidiClip& clip)
    {
        return clip.getSequence().exportToPlaybackMidiSequence (clip, te::MidiList::TimeBase::beatsRaw, false);
    }
}

TEST_SUITE ("core::controllers")
{
    TEST_CASE ("defaults, names and how values read")
    {
        CHECK (controllers::getAll().size() == 3);
        CHECK (controllers::getName (Controller::pitchBend) == "Pitch bend");
        CHECK (controllers::getDefault (Controller::pitchBend) == doctest::Approx (0.5f).epsilon (0.001));
        CHECK (controllers::getDefault (Controller::modWheel) == 0.0f);
        CHECK (controllers::isBipolar (Controller::pitchBend));
        CHECK (controllers::isSwitch (Controller::sustain));

        CHECK (controllers::describe (Controller::pitchBend, 0.5f) == "0% (no bend)");
        CHECK (controllers::describe (Controller::pitchBend, 1.0f) == "+100%");
        CHECK (controllers::describe (Controller::pitchBend, 0.25f) == "-50%");
        CHECK (controllers::describe (Controller::modWheel, 1.0f) == "127");
        CHECK (controllers::describe (Controller::sustain, 0.8f) == "Down");

        const auto bend = controllers::fromMidi (juce::MidiMessage::pitchWheel (1, 16383));
        REQUIRE (bend.has_value());
        CHECK (bend->first == Controller::pitchBend);
        CHECK (bend->second == doctest::Approx (1.0f));
        CHECK (controllers::fromMidi (juce::MidiMessage::controllerEvent (1, 1, 64))->second == doctest::Approx (64.0f / 127.0f));
        CHECK (controllers::fromMidi (juce::MidiMessage::controllerEvent (1, 64, 100))->second == 1.0f);
        CHECK_FALSE (controllers::fromMidi (juce::MidiMessage::controllerEvent (1, 7, 100)).has_value());
        CHECK_FALSE (controllers::fromMidi (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)).has_value());
    }

    TEST_CASE ("a drawn line becomes small steps that play as MIDI; drawing again replaces what was there")
    {
        ClipFixture f;
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::modWheel));
        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 3.0) == 0.0f);

        f.project->beginGesture ("Draw");
        controllers::drawLine (*f.clip, Controller::modWheel, 1.0, 0.0f, 2.0, 1.0f);
        auto points = controllers::getPoints (*f.clip, Controller::modWheel);
        REQUIRE (points.size() >= 30);
        CHECK (points.front().beat > 1.0);                      // 0 at beat 1 changes nothing: not written
        CHECK (points.back().beat == doctest::Approx (2.0));
        CHECK (points.back().value == doctest::Approx (1.0f));

        for (size_t i = 1; i < points.size(); ++i)
            CHECK (points[i].value > points[i - 1].value);       // rising, one step per change

        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 1.5) == doctest::Approx (0.5f).epsilon (0.04));
        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 8.0) == doctest::Approx (1.0f));

        // As played: CC 1 messages.
        const auto midi = playedMidi (*f.clip);
        int ccs = 0;

        for (const auto* e : midi)
            if (e->message.isControllerOfType (1))
                ++ccs;

        CHECK (ccs >= (int) points.size());

        // A flat line through the middle of the ramp.
        controllers::drawLine (*f.clip, Controller::modWheel, 1.75, 0.25f, 1.25, 0.25f);   // either way round
        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 1.5) == doctest::Approx (0.25f).epsilon (0.01));
        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 1.9) > 0.8f);

        // Other controllers aren't touched.
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::pitchBend));

        f.project->undo();
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::modWheel));
        f.project->redo();
        CHECK (controllers::hasPoints (*f.clip, Controller::modWheel));
    }

    TEST_CASE ("pitch bend: up and back to the middle; the pedal only goes up or down")
    {
        ClipFixture f;
        controllers::drawLine (*f.clip, Controller::pitchBend, 0.0, 0.5f, 0.5, 1.0f);
        controllers::drawLine (*f.clip, Controller::pitchBend, 0.5, 1.0f, 1.0, 0.5f);
        CHECK (controllers::getValueAt (*f.clip, Controller::pitchBend, 0.5) == doctest::Approx (1.0f));
        CHECK (controllers::getValueAt (*f.clip, Controller::pitchBend, 4.0) == doctest::Approx (0.5f).epsilon (0.001));

        const auto midi = playedMidi (*f.clip);
        int highest = 0;

        for (const auto* e : midi)
            if (e->message.isPitchWheel())
                highest = std::max (highest, e->message.getPitchWheelValue());

        CHECK (highest == 16383);

        // Down from beat 2 to 4: two events only, down then up.
        controllers::drawLine (*f.clip, Controller::sustain, 2.0, 1.0f, 3.9, 1.0f);
        controllers::add (*f.clip, Controller::sustain, 4.0, 0.0f);
        const auto pedal = controllers::getPoints (*f.clip, Controller::sustain);
        REQUIRE (pedal.size() == 2);
        CHECK (pedal[0] == controllers::Point { 2.0, 1.0f });
        CHECK (pedal[1] == controllers::Point { 4.0, 0.0f });

        controllers::erase (*f.clip, Controller::sustain, 4.5, 1.0);
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::sustain));
    }

    TEST_CASE ("recorded values replace one at the same beat; linked copies get the same")
    {
        ClipFixture f;
        auto* copy = core::clip_links::duplicate (*f.clip, 16.0, true);
        REQUIRE (copy != nullptr);

        controllers::add (*f.clip, Controller::modWheel, 1.0, 0.5f);
        controllers::add (*f.clip, Controller::modWheel, 1.0, 0.75f);
        CHECK (controllers::getPoints (*f.clip, Controller::modWheel).size() == 1);
        CHECK (controllers::getValueAt (*f.clip, Controller::modWheel, 1.0) == doctest::Approx (0.75f).epsilon (0.01));
        CHECK (controllers::getPoints (*copy, Controller::modWheel) == controllers::getPoints (*f.clip, Controller::modWheel));

        controllers::drawLine (*copy, Controller::pitchBend, 0.0, 0.0f, 1.0, 0.5f);
        CHECK (controllers::getPoints (*f.clip, Controller::pitchBend) == controllers::getPoints (*copy, Controller::pitchBend));
    }

    TEST_CASE ("as played in the song: from the clip's start, every time round a loop; baking the loop keeps it")
    {
        ClipFixture f;

        // Hidden events before a trimmed start: the value they set comes first, at the start.
        controllers::set (*f.clip, Controller::modWheel, { { 0.0, 0.25f }, { 2.0, 0.5f }, { 3.0, 0.5f }, { 6.0, 1.0f } });
        CHECK (controllers::getPoints (*f.clip, Controller::modWheel).size() == 3);     // the repeat left out
        core::clips::setStart (*f.clip, 1.0, 0.25);
        core::clips::setEnd (*f.clip, 4.0, 0.25);
        const auto trimmed = controllers::getPlayed (*f.clip, Controller::modWheel);
        REQUIRE (trimmed.size() == 2);
        CHECK (sameAs (trimmed[0], 1.0, 0.25f));
        CHECK (sameAs (trimmed[1], 2.0, 0.5f));

        // A loop of beats 1..4, four times round (to beat 13).
        core::clips::setLooping (*f.clip, true);
        core::clips::setEnd (*f.clip, 13.0, 0.25);
        const auto played = controllers::getPlayed (*f.clip, Controller::modWheel);
        REQUIRE (played.size() == 8);
        CHECK (sameAs (played[0], 1.0, 0.25f));
        CHECK (sameAs (played[1], 2.0, 0.5f));
        CHECK (sameAs (played[2], 4.0, 0.25f));
        CHECK (sameAs (played[7], 11.0, 0.5f));

        core::clips::bakeLoop (*f.clip);
        CHECK_FALSE (core::clips::isLooping (*f.clip));
        CHECK (controllers::getPlayed (*f.clip, Controller::modWheel) == played);
        CHECK_FALSE (controllers::hasPoints (*f.clip, Controller::pitchBend));
    }

    TEST_CASE ("a MIDI file of the clip has its bends and mod wheel; read back, they are the clip's again")
    {
        test::TempFolder temp;
        ClipFixture f;
        core::clips::move (*f.clip, 8.0, *f.project->getTracks().getFirst());
        f.clip->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (4.0), 100, 0, nullptr);
        f.clip->getSequence().addNote (64, te::BeatPosition(), te::BeatDuration::fromBeats (4.0), 100, 2, nullptr);
        controllers::add (*f.clip, Controller::pitchBend, 1.0, 1.0f);
        controllers::add (*f.clip, Controller::pitchBend, 2.0, 0.5f);
        controllers::add (*f.clip, Controller::modWheel, 3.0, 0.5f);

        const auto file = temp.getFile ("Bends.mid");
        REQUIRE (core::midi_files::writeClip (*f.clip, file).isEmpty());

        // Bends reach every note: sent on each channel the notes use.
        juce::MidiFile midi;
        juce::FileInputStream in (file);
        REQUIRE (midi.readFrom (in));
        std::set<int> bendChannels;

        for (int t = 0; t < midi.getNumTracks(); ++t)
            for (const auto* e : *midi.getTrack (t))
                if (e->message.isPitchWheel())
                    bendChannels.insert (e->message.getChannel());

        CHECK (bendChannels == std::set<int> { 1, 3 });

        juce::String error;
        const auto contents = core::midi_files::read (file, error);
        REQUIRE (contents.has_value());
        REQUIRE (contents->parts.size() == 1);
        const auto& events = contents->parts[0].controllerEvents;
        REQUIRE (events.count (Controller::pitchBend) == 1);
        const auto& bend = events.at (Controller::pitchBend);
        REQUIRE (bend.size() == 4);                                 // twice: two channels
        CHECK (bend[0].beat == doctest::Approx (1.0));
        CHECK (bend[0].value == doctest::Approx (1.0f));
        CHECK (events.at (Controller::modWheel).back().beat == doctest::Approx (3.0));

        // Into a clip: the repeats on the second channel change nothing.
        auto* other = f.project->addMidiClip (*f.project->getTracks().getFirst(), 32.0, 4.0, "Read");
        REQUIRE (other != nullptr);
        controllers::set (*other, Controller::pitchBend, bend);
        CHECK (controllers::getPoints (*other, Controller::pitchBend) == controllers::getPoints (*f.clip, Controller::pitchBend));
    }

    TEST_CASE ("controllers are saved with the song")
    {
        ClipFixture f;
        controllers::drawLine (*f.clip, Controller::modWheel, 0.0, 0.2f, 2.0, 0.8f);
        controllers::add (*f.clip, Controller::sustain, 3.0, 1.0f);
        const auto expected = controllers::getPoints (*f.clip, Controller::modWheel);

        juce::TemporaryFile file (".harmonia");
        REQUIRE (f.project->save (file.getFile()).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file.getFile(), error);
        REQUIRE (loaded != nullptr);

        auto* clip = dynamic_cast<te::MidiClip*> (loaded->getTracks().getFirst()->getClips().getFirst());
        REQUIRE (clip != nullptr);
        CHECK (controllers::getPoints (*clip, Controller::modWheel) == expected);
        CHECK (controllers::getValueAt (*clip, Controller::sustain, 3.5) == 1.0f);
    }
}
