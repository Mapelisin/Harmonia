#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestAudioRig.h"
#include "TestSession.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Presets.h"
#include "audio/Sampler.h"
#include "audio/Transport.h"
#include "core/NoteEditor.h"
#include "core/Project.h"

using namespace harmonia;
namespace sampler = audio::sampler;
namespace instruments = audio::instruments;

namespace
{
    constexpr double rate = 48000.0;

    /** A sound in memory: a ramp (every sample of it known) or a 440 Hz tone, mono. */
    audio::SampleData::Ptr makeSample (int length, bool tone = false, double sampleRate = rate)
    {
        audio::SampleData::Ptr data (new audio::SampleData());
        data->sampleRate = sampleRate;
        data->audio.setSize (1, length);

        for (int i = 0; i < length; ++i)
            data->audio.setSample (0, i, tone ? 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sampleRate)
                                              : 0.25f + 0.5f * (float) i / (float) length);

        return data;
    }

    struct Rendered
    {
        std::vector<float> left, right;

        float peak (int from, int to) const
        {
            auto result = 0.0f;

            for (auto i = from; i < std::min (to, (int) left.size()); ++i)
                result = std::max (result, std::abs (left[(size_t) i]));

            return result;
        }

        /** How many times the sound crosses zero between two samples: twice a cycle of a tone. */
        int crossings (int from, int to) const
        {
            int count = 0;

            for (auto i = from + 1; i < std::min (to, (int) left.size()); ++i)
                if ((left[(size_t) i - 1] < 0.0f) != (left[(size_t) i] < 0.0f))
                    ++count;

            return count;
        }
    };

    /** A processor ready to play this sound. */
    struct Machine
    {
        explicit Machine (audio::SampleData::Ptr sample)
        {
            processor.prepare (rate);
            processor.setSample (sample);
            processor.beginBlock();
        }

        Rendered render (int numSamples)
        {
            Rendered result;
            result.left.assign ((size_t) numSamples, 0.0f);
            result.right.assign ((size_t) numSamples, 0.0f);

            for (int at = 0; at < numSamples; at += 256)
            {
                processor.beginBlock();
                processor.render (result.left.data() + at, result.right.data() + at, std::min (256, numSamples - at));
            }

            return result;
        }

        sampler::Processor processor;
        sampler::Settings settings;
    };

    float getRmsDb (const juce::File& file, double from, double to)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        const auto start = (juce::int64) (from * reader->sampleRate);
        const auto length = (int) ((to - from) * reader->sampleRate);
        juce::AudioBuffer<float> buffer (2, length);
        reader->read (&buffer, 0, length, start, true, true);
        return juce::Decibels::gainToDecibels (buffer.getRMSLevel (0, 0, length));
    }

    /** A song whose first track plays the sampler, and sound files for it. */
    struct SamplerSong
    {
        SamplerSong()
        {
            REQUIRE (instruments::useSampler (track).isEmpty());
            instrument = dynamic_cast<sampler::Plugin*> (instruments::get (track));
            REQUIRE (instrument != nullptr);
            project->clearUndoHistory();
        }

        void addNote (int pitch, double startBeat, double lengthBeats)
        {
            if (clip == nullptr)
                clip = project->addMidiClip (track, 0.0, 8.0, "Notes");

            core::Note note;
            note.pitch = pitch;
            note.startBeat = startBeat;
            note.lengthBeats = lengthBeats;
            note.velocity = 127;
            core::NoteEditor (*clip).add ({ note });
            project->getEdit().dispatchPendingUpdatesSynchronously();
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack& track = *project->getTracks().getFirst();
        sampler::Plugin* instrument = nullptr;
        te::MidiClip* clip = nullptr;
        const juce::File tone = test::writeTone (temp.getFile ("Pluck.wav"), 2.0, 0.5f);
        const juce::File other = test::writeTone (temp.getFile ("Pad 7.wav"), 1.0, 0.25f);
    };
}

TEST_SUITE ("audio: the sampler")
{
    TEST_CASE ("the root key plays the sound as it is; an octave up twice as fast, an octave down half as fast")
    {
        const auto ramp = makeSample (2000);
        Machine machine (ramp);
        machine.settings.oneShot = true;

        machine.processor.noteOn (sampler::defaultRoot, 1.0f, 1, machine.settings);
        auto out = machine.render (3000);

        for (int i : { 0, 1, 500, 1999 })
        {
            CHECK (out.left[(size_t) i] == doctest::Approx (ramp->audio.getSample (0, i)).epsilon (0.001));
            CHECK (out.right[(size_t) i] == doctest::Approx (ramp->audio.getSample (0, i)).epsilon (0.001));
        }

        CHECK (out.peak (2000, 3000) == 0.0f);
        CHECK (machine.processor.getNumPlaying() == 0);

        machine.processor.noteOn (sampler::defaultRoot + 12, 1.0f, 1, machine.settings);
        out = machine.render (3000);
        CHECK (out.left[500] == doctest::Approx (ramp->audio.getSample (0, 1000)).epsilon (0.001));
        CHECK (out.peak (1000, 3000) == 0.0f);

        machine.processor.noteOn (sampler::defaultRoot - 12, 1.0f, 1, machine.settings);
        out = machine.render (5000);
        CHECK (out.left[2000] == doctest::Approx (ramp->audio.getSample (0, 1000)).epsilon (0.001));
        CHECK (out.peak (3900, 3999) > 0.5f);
        CHECK (out.peak (4000, 5000) == 0.0f);

        // Another root key, Tune, and a file at another rate.
        machine.settings.root = 48;
        machine.settings.tune = 12.0f;
        machine.processor.noteOn (48, 1.0f, 1, machine.settings);
        CHECK (machine.render (3000).peak (1000, 3000) == 0.0f);       // an octave up by Tune: over in half the time

        Machine slow (makeSample (1000, false, rate / 2.0));
        slow.settings.oneShot = true;
        slow.processor.noteOn (sampler::defaultRoot, 1.0f, 1, slow.settings);
        out = slow.render (3000);
        CHECK (out.peak (1900, 1999) > 0.5f);                          // as long as at its own rate
        CHECK (out.peak (2000, 3000) == 0.0f);

        // No sound set: silence.
        Machine empty (nullptr);
        empty.processor.noteOn (60, 1.0f, 1, empty.settings);
        CHECK (empty.processor.getNumPlaying() == 0);
    }

    TEST_CASE ("a key let go fades out by Release; One shot plays on; Attack fades in; harder is louder")
    {
        Machine machine (makeSample (48000, true));
        machine.settings.releaseSeconds = 0.05;

        machine.processor.noteOn (60, 1.0f, 1, machine.settings);
        machine.render (4800);
        machine.processor.noteOff (60, 1);
        auto out = machine.render (9600);
        CHECK (out.peak (0, 480) > 0.3f);               // still there right after
        CHECK (out.peak (2400, 2880) < 0.01f);          // gone after the release
        CHECK (machine.processor.getNumPlaying() == 0);

        // One shot: letting go changes nothing.
        machine.settings.oneShot = true;
        machine.processor.noteOn (60, 1.0f, 1, machine.settings);
        machine.render (4800);
        machine.processor.noteOff (60, 1);
        out = machine.render (9600);
        CHECK (out.peak (7200, 9600) > 0.4f);
        machine.processor.allNotesOff();
        machine.render (9600);
        CHECK (machine.processor.getNumPlaying() == 0);

        // Attack: quiet at first, full after it.
        machine.settings.oneShot = false;
        machine.settings.attackSeconds = 0.1;
        machine.processor.noteOn (60, 1.0f, 1, machine.settings);
        out = machine.render (9600);
        CHECK (out.peak (0, 480) < 0.06f);
        CHECK (out.peak (7200, 9600) > 0.45f);
        machine.processor.reset();

        machine.settings.attackSeconds = 0.0;
        machine.processor.noteOn (60, 0.5f, 1, machine.settings);
        const auto soft = machine.render (4800).peak (0, 4800);
        machine.processor.reset();
        machine.processor.noteOn (60, 1.0f, 1, machine.settings);
        const auto hard = machine.render (4800).peak (0, 4800);
        CHECK (hard == doctest::Approx (0.5f).epsilon (0.02));
        CHECK (soft < hard * 0.5f);

        machine.settings.gain = 0.5f;
        machine.processor.reset();
        machine.processor.noteOn (60, 1.0f, 1, machine.settings);
        CHECK (machine.render (4800).peak (0, 4800) == doctest::Approx (hard * 0.5f).epsilon (0.02));
    }

    TEST_CASE ("several notes at once; the pedal holds notes let go; the pitch wheel bends the notes of its channel")
    {
        Machine machine (makeSample (96000, true));
        machine.settings.releaseSeconds = 0.01;

        for (const auto note : { 60, 64, 67 })
            machine.processor.noteOn (note, 0.8f, 1, machine.settings);

        machine.render (480);
        CHECK (machine.processor.getNumPlaying() == 3);

        // The same key again: the old note fades out under the new one.
        machine.processor.noteOn (60, 0.8f, 1, machine.settings);
        machine.render (4800);
        CHECK (machine.processor.getNumPlaying() == 3);

        // The pedal down: the keys let go keep sounding until it comes up.
        machine.processor.setSustain (1, true);
        machine.processor.noteOff (60, 1);
        machine.processor.noteOff (64, 1);
        machine.render (4800);
        CHECK (machine.processor.getNumPlaying() == 3);
        machine.processor.setSustain (1, false);
        machine.render (4800);
        CHECK (machine.processor.getNumPlaying() == 1);
        machine.processor.reset();

        // Bent up a tone: more cycles in the same time; another channel's notes aren't.
        machine.processor.noteOn (60, 1.0f, 1, machine.settings);
        const auto straight = machine.render (48000).crossings (0, 48000);
        machine.processor.reset();
        machine.processor.setPitchBend (1, sampler::bendSemitones);
        machine.processor.noteOn (60, 1.0f, 1, machine.settings);
        const auto bent = machine.render (48000).crossings (0, 48000);
        CHECK (straight == doctest::Approx (880).epsilon (0.01));
        CHECK (bent == doctest::Approx (880.0 * std::pow (2.0, 2.0 / 12.0)).epsilon (0.01));

        machine.processor.allNotesOff();
        machine.render (4800);
        machine.processor.noteOn (60, 1.0f, 2, machine.settings);
        CHECK (machine.render (48000).crossings (0, 48000) == doctest::Approx (880).epsilon (0.01));

        // More notes than voices: the oldest give way.
        machine.processor.reset();

        for (int i = 0; i < sampler::Processor::maxVoices + 8; ++i)
            machine.processor.noteOn (30 + i, 0.5f, 1, machine.settings);

        CHECK (machine.processor.getNumPlaying() == sampler::Processor::maxVoices);

        // A new sound: the notes of the old one stop.
        machine.processor.setSample (makeSample (4800, true));
        machine.render (256);
        CHECK (machine.processor.getNumPlaying() == 0);
    }

    TEST_CASE ("the song keeps the sampler's sound and root key; one undo step each; presets have them")
    {
        SamplerSong song;
        auto& plugin = *song.instrument;

        CHECK (instruments::playsSampler (song.track));
        CHECK (instruments::getName (song.track) == "Sampler");
        CHECK (sampler::getFile (plugin) == juce::File());
        CHECK_FALSE (sampler::isFileMissing (plugin));
        CHECK (sampler::getRoot (plugin) == sampler::defaultRoot);
        CHECK (plugin.getSample() == nullptr);

        juce::StringArray names;

        for (auto* parameter : plugin.getAutomatableParameters())
            names.add (parameter->getParameterName());

        CHECK (names.joinIntoString (", ") == "Volume, Tune, Attack, Release, Mode");
        CHECK (plugin.mode->getCurrentValueAsString() == "Hold the key");
        CHECK (plugin.release->getCurrentValueAsString() == "100 ms");

        REQUIRE (sampler::setFile (plugin, song.tone).isEmpty());
        CHECK (sampler::getFile (plugin) == song.tone);
        CHECK (song.project->getUndoDescription() == "Sampler: Pluck");
        plugin.updateSampleNow();
        REQUIRE (plugin.getSample() != nullptr);
        CHECK (plugin.getSample()->getLengthSeconds() == doctest::Approx (2.0).epsilon (0.01));

        song.project->beginGesture ("Root key");
        sampler::setRoot (plugin, 48);
        CHECK (sampler::getRoot (plugin) == 48);
        sampler::setRoot (plugin, 500);
        CHECK (sampler::getRoot (plugin) == 127);
        song.project->undo();
        CHECK (sampler::getRoot (plugin) == sampler::defaultRoot);

        // What isn't a sound: said so, nothing changed.
        const auto text = song.temp.getFile ("Notes.txt");
        text.replaceWithText ("words");
        CHECK (sampler::setFile (plugin, text).isNotEmpty());
        CHECK (sampler::getFile (plugin) == song.tone);

        // A preset has the sound, the root key and the knobs.
        const auto presetsFolder = song.temp.getFile ("Presets");
        song.project->beginGesture ("Settings");
        sampler::setRoot (plugin, 55);
        plugin.release->setParameter (800.0f, juce::sendNotificationSync);
        REQUIRE (audio::presets::canHavePresets (plugin));
        REQUIRE (audio::presets::save (plugin, "My pluck", presetsFolder).isEmpty());

        audio::presets::reset (plugin);
        CHECK (sampler::getFile (plugin) == juce::File());
        CHECK (sampler::getRoot (plugin) == sampler::defaultRoot);

        const auto presets = audio::presets::list (plugin, presetsFolder);
        REQUIRE (presets.size() == 1);
        REQUIRE (audio::presets::load (plugin, presets[0]).isEmpty());
        CHECK (sampler::getFile (plugin) == song.tone);
        CHECK (sampler::getRoot (plugin) == 55);
        CHECK (plugin.release->getCurrentValue() == doctest::Approx (800.0f));

        // Saved with the song.
        const auto file = song.temp.getFile ("Song.harmonia");
        REQUIRE (song.project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        auto* again = dynamic_cast<sampler::Plugin*> (instruments::get (*loaded->getTracks().getFirst()));
        REQUIRE (again != nullptr);
        CHECK (sampler::getFile (*again) == song.tone);
        CHECK (sampler::getRoot (*again) == 55);
        CHECK (again->getSample() != nullptr);
        loaded.reset();

        // The file gone: said so.
        REQUIRE (sampler::setFile (plugin, song.other).isEmpty());
        song.other.deleteFile();
        CHECK (sampler::isFileMissing (plugin));

        // A track given the sampler with a sound that can't be read keeps its instrument.
        auto& second = *song.project->addTrack ("Second");
        REQUIRE (instruments::useBuiltInSynth (second).isEmpty());
        CHECK (instruments::useSampler (second, text).isNotEmpty());
        CHECK_FALSE (instruments::playsSampler (second));
        CHECK (instruments::useSampler (second, song.tone).isEmpty());
        CHECK (instruments::playsSampler (second));
        CHECK (sampler::getFile (*instruments::get (second)) == song.tone);
    }

    TEST_CASE ("in the exported song the notes play the sound: at the root as it is, shorter notes cut it")
    {
        SamplerSong song;
        auto& plugin = *song.instrument;
        song.addNote (60, 0.0, 1.0);        // half a second at 120 BPM
        song.addNote (72, 4.0, 2.0);        // from second 2, a second long

        // Nothing to play yet: silence.
        const auto silent = song.temp.getFile ("Silent.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), silent).isEmpty());
        CHECK (getRmsDb (silent, 0.0, 0.5) < -80.0f);

        REQUIRE (sampler::setFile (plugin, song.tone).isEmpty());
        plugin.updateSampleNow();
        const auto played = song.temp.getFile ("Played.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), played).isEmpty());
        CHECK (getRmsDb (played, 0.05, 0.45) > -13.0f);     // the tone, while the key is down
        CHECK (getRmsDb (played, 1.0, 1.8) < -70.0f);       // let go: faded out (the file would go on)
        CHECK (getRmsDb (played, 2.1, 2.9) > -13.0f);       // the second note

        // One shot: the first note rings to the end of the two-second file.
        song.project->beginGesture ("Mode");
        plugin.mode->setParameter (1.0f, juce::sendNotificationSync);
        const auto oneShot = song.temp.getFile ("OneShot.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), oneShot).isEmpty());
        CHECK (getRmsDb (oneShot, 1.0, 1.8) > -13.0f);

        // Undo back to no sound: silence again (the sound follows by itself, a moment later).
        song.project->undo();
        song.project->undo();
        CHECK (sampler::getFile (plugin) == juce::File());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto after = song.temp.getFile ("After.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), after).isEmpty());
        CHECK (getRmsDb (after, 0.0, 0.5) < -80.0f);
    }

    TEST_CASE ("played live: a key sounds while it is held (or the pedal is); another sound can come while it plays")
    {
        test::AudioRig rig;
        auto& track = *rig.project->getTracks().getFirst();
        REQUIRE (instruments::useSampler (track).isEmpty());
        auto* plugin = dynamic_cast<sampler::Plugin*> (instruments::get (track));
        REQUIRE (plugin != nullptr);

        const auto tone = test::writeTone (rig.temp.getFile ("Long.wav"), 6.0, 0.5f);
        const auto other = test::writeTone (rig.temp.getFile ("Other.wav"), 6.0, 0.25f);
        audio::Transport transport (rig.project->getEdit());       // as the window has: the song is ready to sound
        const auto source = te::createUniqueMPESourceID();
        const auto send = [&track, source] (const juce::MidiMessage& message) { track.injectLiveMidiMessage (message, source); };

        // No sound yet: a key plays nothing.
        rig.process (0.2, 0.0f);
        send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
        CHECK (rig.process (0.3, 0.0f) < 1.0e-4f);
        send (juce::MidiMessage::noteOff (1, 60));

        REQUIRE (sampler::setFile (*plugin, tone).isEmpty());
        plugin->updateSampleNow();
        rig.process (0.1, 0.0f);

        send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
        rig.process (0.05, 0.0f);
        const auto held = rig.process (0.3, 0.0f);
        CHECK (held > 0.1f);

        // The pedal down, the key let go: it goes on; the pedal up: it fades out.
        send (juce::MidiMessage::controllerEvent (1, 64, 127));
        send (juce::MidiMessage::noteOff (1, 60));
        rig.process (0.3, 0.0f);
        CHECK (rig.process (0.3, 0.0f) == doctest::Approx (held).epsilon (0.05));
        send (juce::MidiMessage::controllerEvent (1, 64, 0));
        rig.process (0.4, 0.0f);
        CHECK (rig.process (0.2, 0.0f) < 1.0e-4f);

        // Another sound while a key is held (as a preset or an undo brings it): the note of the old
        // one stops, the next key plays the new one.
        send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
        rig.process (0.2, 0.0f);
        REQUIRE (sampler::setFile (*plugin, other).isEmpty());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);       // it follows by itself
        rig.process (0.2, 0.0f);
        CHECK (rig.process (0.2, 0.0f) < 1.0e-4f);
        send (juce::MidiMessage::noteOff (1, 60));
        send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
        rig.process (0.05, 0.0f);
        CHECK (rig.process (0.3, 0.0f) == doctest::Approx (held * 0.5f).epsilon (0.05));
        send (juce::MidiMessage::noteOff (1, 60));
        rig.process (0.4, 0.0f);

        // Undo back to the first sound, and to none.
        rig.project->undo();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK (sampler::getFile (*plugin) == tone);
        send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
        rig.process (0.05, 0.0f);
        CHECK (rig.process (0.3, 0.0f) == doctest::Approx (held).epsilon (0.05));
        send (juce::MidiMessage::noteOff (1, 60));
        rig.process (0.4, 0.0f);
    }
}
