#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/MidiRender.h"

using namespace harmonia;
namespace midi_render = audio::midi_render;

namespace
{
    constexpr double rate = midi_render::defaultSampleRate;
    constexpr int ticks = 480;      // per beat

    juce::MidiFile fileOf (const juce::MidiMessageSequence& events)
    {
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ticks);
        file.addTrack (events);
        return file;
    }

    /** A note from `startBeat`, `beats` long (120 BPM unless the file says otherwise). */
    void addNote (juce::MidiMessageSequence& events, int channel, int pitch, double startBeat, double beats)
    {
        events.addEvent (juce::MidiMessage::noteOn (channel, pitch, (juce::uint8) 100), startBeat * ticks);
        events.addEvent (juce::MidiMessage::noteOff (channel, pitch), (startBeat + beats) * ticks);
    }

    float rms (const juce::AudioBuffer<float>& buffer, double from, double to)
    {
        const auto start = (int) (from * rate);
        const auto end = std::min (buffer.getNumSamples(), (int) (to * rate));
        return end > start ? buffer.getRMSLevel (0, start, end - start) : 0.0f;
    }
}

TEST_SUITE ("audio::midi_render")
{
    TEST_CASE ("a note sounds at its pitch while it is held, then rings out and stops")
    {
        juce::MidiMessageSequence events;
        addNote (events, 1, 60, 0.0, 1.0);      // half a second at 120 BPM
        const auto sound = midi_render::render (fileOf (events));

        REQUIRE (sound.getNumChannels() == 2);
        CHECK (sound.getNumSamples() == (int) std::ceil (1.5 * rate));     // the note and a second to ring out
        CHECK (rms (sound, 0.05, 0.45) > 0.02f);
        CHECK (rms (sound, 0.9, 1.5) < 0.0001f);
        CHECK (sound.getMagnitude (0, sound.getNumSamples()) <= 0.8f + 1.0e-4f);

        // Middle C: 261.6 Hz, two zero crossings a cycle.
        int crossings = 0;

        for (auto i = (int) (0.1 * rate); i < (int) (0.4 * rate); ++i)
            if ((sound.getSample (0, i - 1) < 0.0f) != (sound.getSample (0, i) < 0.0f))
                ++crossings;

        CHECK (crossings / (2.0 * 0.3) == doctest::Approx (261.6).epsilon (0.03));
    }

    TEST_CASE ("the file's tempo places the notes; channel 10 plays drums, which ring out on their own")
    {
        juce::MidiMessageSequence events;
        events.addEvent (juce::MidiMessage::tempoMetaEvent (1000000), 0.0);       // 60 BPM: a beat is a second
        addNote (events, 1, 64, 1.0, 1.0);
        const auto slow = midi_render::render (fileOf (events));
        CHECK (rms (slow, 0.0, 0.95) < 0.0001f);
        CHECK (rms (slow, 1.05, 1.9) > 0.02f);

        juce::MidiMessageSequence drums;
        drums.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 127), 0.0);     // a kick, no note-off
        drums.addEvent (juce::MidiMessage::noteOn (10, 42, (juce::uint8) 100), 2.0 * ticks); // a closed hat at 1 s
        const auto beat = midi_render::render (fileOf (drums));
        CHECK (rms (beat, 0.0, 0.1) > 0.05f);
        CHECK (rms (beat, 0.5, 0.95) < 0.0001f);       // the kick is over
        CHECK (rms (beat, 1.0, 1.04) > 0.01f);
        CHECK (rms (beat, 1.2, 2.0) < 0.0001f);        // and the hat is short
    }

    TEST_CASE ("many notes at once don't clip; a long file stops after a minute, fading out")
    {
        juce::MidiMessageSequence cluster;

        for (int pitch = 30; pitch < 90; ++pitch)
            addNote (cluster, 1, pitch, 0.0, 2.0);

        const auto loud = midi_render::render (fileOf (cluster));
        CHECK (loud.getMagnitude (0, loud.getNumSamples()) <= 0.8f + 1.0e-4f);
        CHECK (rms (loud, 0.1, 0.9) > 0.01f);

        juce::MidiMessageSequence longNote;
        addNote (longNote, 1, 48, 0.0, 400.0);      // 200 seconds
        const auto start = midi_render::render (fileOf (longNote));
        CHECK (start.getNumSamples() == (int) std::ceil (midi_render::maxSeconds * rate));
        CHECK (std::abs (start.getSample (0, start.getNumSamples() - 1)) < 0.001f);
        CHECK (rms (start, 59.0, 59.9) > 0.01f);
    }

    TEST_CASE ("nothing to play: no notes, or not a MIDI file")
    {
        juce::MidiMessageSequence tempoOnly;
        tempoOnly.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
        CHECK (midi_render::render (fileOf (tempoOnly)).getNumSamples() == 0);

        test::TempFolder temp;
        const auto text = temp.getFile ("notes.mid");
        text.replaceWithText ("not MIDI");
        CHECK (midi_render::render (text).getNumSamples() == 0);
        CHECK (midi_render::render (temp.getFile ("missing.mid")).getNumSamples() == 0);
    }
}
