#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "audio/DrumKit.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Presets.h"
#include "core/NoteEditor.h"
#include "core/PluginState.h"
#include "core/Project.h"

using namespace harmonia;
namespace drum_kit = audio::drum_kit;
namespace instruments = audio::instruments;
using drum_kit::Kit;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int kick = 36, closedHat = 42, openHat = 46, spare = 100;     // 100: no sound of the kit's own

    /** A sample in memory: a ramp (so every sample of it is known) or a tone. */
    drum_kit::SampleData::Ptr makeSample (int length, int channels = 1, double sampleRate = rate, bool tone = false)
    {
        drum_kit::SampleData::Ptr data (new drum_kit::SampleData());
        data->sampleRate = sampleRate;
        data->audio.setSize (channels, length);

        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < length; ++i)
                data->audio.setSample (c, i, tone ? 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sampleRate)
                                                  : (c == 0 ? 0.25f : -0.25f) + 0.5f * (float) i / (float) length);

        return data;
    }

    drum_kit::SampleBank::Ptr makeBank (std::initializer_list<std::pair<int, drum_kit::KeySample>> keys)
    {
        drum_kit::SampleBank::Ptr bank (new drum_kit::SampleBank());

        for (const auto& [note, sample] : keys)
            bank->keys[(size_t) note] = sample;

        return bank;
    }

    struct Rendered
    {
        std::vector<float> left, right;

        float peak (int from, int to, bool rightSide = false) const
        {
            const auto& side = rightSide ? right : left;
            auto result = 0.0f;

            for (auto i = from; i < std::min (to, (int) side.size()); ++i)
                result = std::max (result, std::abs (side[(size_t) i]));

            return result;
        }
    };

    /** The next `numSamples` of the processor, in blocks as an audio interface asks for them. */
    Rendered render (drum_kit::Processor& processor, int numSamples)
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

    /** A processor ready to play, with these samples taken. */
    struct Machine
    {
        explicit Machine (drum_kit::SampleBank::Ptr bank)
        {
            processor.prepare (rate);
            processor.setSamples (bank);
            processor.beginBlock();
        }

        void hit (int note, float velocity = 1.0f, float tune = 0.0f, float decay = 1.0f)
        {
            processor.noteOn (note, velocity, Kit::punchy, tune, decay);
        }

        drum_kit::Processor processor;
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

    /** A song whose first track plays the drum machine, and two sound files to put on its keys. */
    struct DrumSong
    {
        DrumSong()
        {
            REQUIRE (instruments::useDrumKit (track).isEmpty());
            drums = dynamic_cast<drum_kit::Plugin*> (instruments::get (track));
            REQUIRE (drums != nullptr);
            project->clearUndoHistory();
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack& track = *project->getTracks().getFirst();
        drum_kit::Plugin* drums = nullptr;
        const juce::File tone = test::writeTone (temp.getFile ("My kick.wav"), 0.5, 0.5f);
        const juce::File other = test::writeTone (temp.getFile ("Clap 2.wav"), 0.25, 0.25f);
    };
}

TEST_SUITE ("audio: samples in the drum machine")
{
    TEST_CASE ("a key with a sample plays the file as it is, to its end; a stereo file on both sides")
    {
        const auto mono = makeSample (1000);
        const auto stereo = makeSample (600, 2);
        Machine machine (makeBank ({ { kick, { mono } }, { spare, { stereo } } }));

        machine.hit (kick);
        auto out = render (machine.processor, 1500);

        for (int i : { 0, 1, 250, 999 })
        {
            CHECK (out.left[(size_t) i] == doctest::Approx (mono->audio.getSample (0, i)).epsilon (0.001));
            CHECK (out.right[(size_t) i] == doctest::Approx (mono->audio.getSample (0, i)).epsilon (0.001));
        }

        CHECK (out.peak (1000, 1500) == 0.0f);
        CHECK (machine.processor.getNumPlaying() == 0);

        // A key the kit has no sound for plays its sample too; without one it's silent.
        machine.hit (spare);
        out = render (machine.processor, 800);
        CHECK (out.left[300] == doctest::Approx (stereo->audio.getSample (0, 300)).epsilon (0.001));
        CHECK (out.right[300] == doctest::Approx (stereo->audio.getSample (1, 300)).epsilon (0.001));

        machine.hit (spare + 1);
        CHECK (render (machine.processor, 800).peak (0, 800) == 0.0f);
    }

    TEST_CASE ("velocity and the sample's own volume set how loud; tuning and the file's rate how fast")
    {
        const auto sample = makeSample (4800, 1, rate, true);
        const auto slowFile = makeSample (2400, 1, rate / 2.0, true);     // the same tone, at half the rate
        Machine machine (makeBank ({ { kick, { sample } }, { 37, { sample, 0.5f } }, { 38, { sample, 1.0f, 12.0f } }, { 39, { slowFile } } }));

        machine.hit (kick);
        const auto full = render (machine.processor, 6000).peak (0, 4800);
        CHECK (full == doctest::Approx (0.5f).epsilon (0.02));

        machine.hit (kick, 0.5f);
        const auto soft = render (machine.processor, 6000).peak (0, 4800);
        CHECK (soft < full * 0.5f);
        CHECK (soft > full * 0.2f);

        machine.hit (37);
        CHECK (render (machine.processor, 6000).peak (0, 4800) == doctest::Approx (full * 0.5f).epsilon (0.02));

        // An octave up: twice as fast, over in half the time. The kit's Tune adds to the key's.
        machine.hit (38);
        auto out = render (machine.processor, 6000);
        CHECK (out.peak (0, 2300) > 0.4f);
        CHECK (out.peak (2450, 6000) == 0.0f);

        machine.hit (kick, 1.0f, -12.0f);
        out = render (machine.processor, 12000);
        CHECK (out.peak (9000, 9500) > 0.4f);       // still going after its own length
        CHECK (out.peak (9700, 12000) == 0.0f);

        // A file at half the song's rate lasts as long as it does at its own.
        machine.hit (39);
        out = render (machine.processor, 6000);
        CHECK (out.peak (4000, 4700) > 0.4f);
        CHECK (out.peak (4900, 6000) == 0.0f);
    }

    TEST_CASE ("hit again a sample stops ringing; a closed hat stops a sampled open hat; Decay cuts samples short")
    {
        const auto longSample = makeSample (48000, 1, rate, true);
        Machine machine (makeBank ({ { kick, { longSample } }, { openHat, { longSample } } }));

        machine.hit (kick);
        render (machine.processor, 4800);
        machine.hit (kick);
        render (machine.processor, 4800);
        CHECK (machine.processor.getNumPlaying() == 1);     // the first one faded out

        machine.processor.reset();
        machine.hit (openHat);
        render (machine.processor, 4800);
        machine.hit (closedHat);                             // the kit's own closed hat
        const auto out = render (machine.processor, 48000);
        CHECK (out.peak (24000, 48000) < 0.001f);            // nothing of the open hat's tone left

        // Half the Decay: half the sample, fading out; everything stops on All notes off.
        machine.processor.reset();
        machine.hit (kick, 1.0f, 0.0f, 0.5f);
        const auto cut = render (machine.processor, 48000);
        CHECK (cut.peak (0, 12000) > 0.4f);
        CHECK (cut.peak (23000, 23900) < 0.1f);
        CHECK (cut.peak (24100, 48000) == 0.0f);

        machine.hit (kick);
        render (machine.processor, 2400);
        machine.processor.stopAll();
        render (machine.processor, 2400);
        CHECK (machine.processor.getNumPlaying() == 0);
    }

    TEST_CASE ("new samples: a hit whose sample went stops, the others play on")
    {
        const auto kept = makeSample (48000, 1, rate, true);
        const auto dropped = makeSample (48000, 1, rate, true);
        Machine machine (makeBank ({ { kick, { kept } }, { spare, { dropped } } }));

        machine.hit (kick);
        machine.hit (spare);
        render (machine.processor, 2400);
        CHECK (machine.processor.getNumPlaying() == 2);

        machine.processor.setSamples (makeBank ({ { kick, { kept, 0.5f } } }));
        render (machine.processor, 2400);
        CHECK (machine.processor.getNumPlaying() == 1);

        // The key that lost its sample: silent (no sound of the kit's own there).
        machine.hit (spare);
        CHECK (machine.processor.getNumPlaying() == 1);

        machine.processor.setSamples (nullptr);
        render (machine.processor, 2400);
        CHECK (machine.processor.getNumPlaying() == 0);
    }

    TEST_CASE ("the song keeps which key plays which file, its volume and tuning; one undo step each")
    {
        DrumSong song;
        auto& drums = *song.drums;

        CHECK_FALSE (drum_kit::getSample (drums, kick).has_value());
        CHECK (drum_kit::getSampledKeys (drums).empty());
        CHECK (drum_kit::getKeyName (drums, kick) == "Kick");
        CHECK (drum_kit::getKeyName (drums, spare).isEmpty());

        REQUIRE (drum_kit::setSample (drums, kick, song.tone).isEmpty());
        REQUIRE (drum_kit::getSample (drums, kick).has_value());
        CHECK (drum_kit::getSample (drums, kick)->file == song.tone);
        CHECK (drum_kit::getSample (drums, kick)->gainDb == doctest::Approx (0.0f));
        CHECK_FALSE (drum_kit::getSample (drums, kick)->missing);
        CHECK (drum_kit::getKeyName (drums, kick) == "My kick");
        CHECK (song.project->getUndoDescription() == "Sample for Kick");
        CHECK (song.project->hasUnsavedChanges());

        REQUIRE (drum_kit::setSample (drums, spare, song.other).isEmpty());
        CHECK (drum_kit::getSampledKeys (drums) == std::vector<int> { kick, spare });
        CHECK (drum_kit::getKeyName (drums, spare) == "Clap 2");

        song.project->beginGesture ("Sample volume");
        drum_kit::setSampleGainDb (drums, kick, -6.0f);
        drum_kit::setSampleTune (drums, kick, 100.0f);      // not past two octaves
        CHECK (drum_kit::getSample (drums, kick)->gainDb == doctest::Approx (-6.0f));
        CHECK (drum_kit::getSample (drums, kick)->tune == doctest::Approx (drum_kit::maxSampleTune));
        song.project->undo();
        CHECK (drum_kit::getSample (drums, kick)->gainDb == doctest::Approx (0.0f));

        // Another file on the key: its volume and tuning start afresh.
        song.project->beginGesture ("Sample volume");
        drum_kit::setSampleGainDb (drums, kick, -6.0f);
        REQUIRE (drum_kit::setSample (drums, kick, song.other).isEmpty());
        CHECK (drum_kit::getSample (drums, kick)->file == song.other);
        CHECK (drum_kit::getSample (drums, kick)->gainDb == doctest::Approx (0.0f));
        song.project->undo();
        CHECK (drum_kit::getSample (drums, kick)->file == song.tone);

        // What can't be used says why, and changes nothing.
        const auto text = song.temp.getFile ("Notes.txt");
        text.replaceWithText ("not a sound");
        CHECK (drum_kit::setSample (drums, 40, text).isNotEmpty());
        CHECK (drum_kit::setSample (drums, 40, song.temp.getFile ("Nothing.wav")).isNotEmpty());
        CHECK (drum_kit::setSample (drums, 200, song.tone).isNotEmpty());
        CHECK_FALSE (drum_kit::getSample (drums, 40).has_value());

        drum_kit::clearSample (drums, kick);
        CHECK_FALSE (drum_kit::getSample (drums, kick).has_value());
        CHECK (drum_kit::getKeyName (drums, kick) == "Kick");
        CHECK (song.project->getUndoDescription() == "Built-in sound for Kick");
        song.project->undo();
        CHECK (drum_kit::getSample (drums, kick).has_value());

        // Saved with the song; a copy of the drum machine has them too.
        const auto file = song.temp.getFile ("Song.harmonia");
        REQUIRE (song.project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        auto* again = instruments::get (*loaded->getTracks().getFirst());
        REQUIRE (again != nullptr);
        CHECK (drum_kit::getSampledKeys (*again) == std::vector<int> { kick, spare });
        CHECK (drum_kit::getSample (*again, kick)->file == song.tone);

        const auto copy = core::plugin_state::snapshot (drums);
        CHECK (copy.getChildWithName (drum_kit::getSamplesTag()).getNumChildren() == 2);

        // The file gone: the key says so and is the kit's own again.
        loaded.reset();
        song.other.deleteFile();
        CHECK (drum_kit::getSample (drums, spare)->missing);
        CHECK (drum_kit::getKeyName (drums, spare).isEmpty());
    }

    TEST_CASE ("in the exported song a sampled key sounds like its file; back to the kit's own sound, like the kit")
    {
        DrumSong song;
        auto& drums = *song.drums;
        auto* clip = song.project->addMidiClip (song.track, 0.0, 4.0, "Beat");
        REQUIRE (clip != nullptr);

        core::Note note;
        note.pitch = spare;
        note.startBeat = 0.0;
        note.lengthBeats = 1.0;
        note.velocity = 127;
        core::NoteEditor (*clip).add ({ note });
        song.project->getEdit().dispatchPendingUpdatesSynchronously();

        // The key has no sound of the kit's own: silence.
        const auto silent = song.temp.getFile ("Silent.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), silent).isEmpty());
        CHECK (getRmsDb (silent, 0.0, 0.4) < -80.0f);

        // With the half-second tone on it: the tone, at about the file's level.
        REQUIRE (drum_kit::setSample (drums, spare, song.tone).isEmpty());
        drums.updateSamplesNow();
        const auto sampled = song.temp.getFile ("Sampled.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), sampled).isEmpty());
        CHECK (getRmsDb (sampled, 0.05, 0.45) > -14.0f);
        CHECK (getRmsDb (sampled, 0.05, 0.45) < -6.0f);
        CHECK (getRmsDb (sampled, 0.6, 1.0) < -80.0f);       // the file has ended

        // Quieter by its own volume.
        song.project->beginGesture ("Sample volume");
        drum_kit::setSampleGainDb (drums, spare, -12.0f);
        drums.updateSamplesNow();
        const auto quiet = song.temp.getFile ("Quiet.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), quiet).isEmpty());
        CHECK (getRmsDb (quiet, 0.05, 0.45) == doctest::Approx (getRmsDb (sampled, 0.05, 0.45) - 12.0f).epsilon (0.05));

        // Undo: silent again (the sound follows a moment after the change, by itself).
        song.project->undo();
        song.project->undo();
        CHECK_FALSE (drum_kit::getSample (drums, spare).has_value());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto after = song.temp.getFile ("After.wav");
        REQUIRE (audio::exportWav (song.project->getEdit(), after).isEmpty());
        CHECK (getRmsDb (after, 0.0, 0.4) < -80.0f);
    }

    TEST_CASE ("a preset of the drum machine has its samples; the first settings have none")
    {
        DrumSong song;
        auto& drums = *song.drums;
        const auto presetsFolder = song.temp.getFile ("Presets");

        REQUIRE (drum_kit::setSample (drums, kick, song.tone).isEmpty());
        song.project->beginGesture ("Sample volume");
        drum_kit::setSampleGainDb (drums, kick, -3.0f);
        REQUIRE (audio::presets::save (drums, "My kit", presetsFolder).isEmpty());

        audio::presets::reset (drums);
        CHECK (drum_kit::getSampledKeys (drums).empty());

        const auto presets = audio::presets::list (drums, presetsFolder);
        REQUIRE (presets.size() == 1);
        REQUIRE (audio::presets::load (drums, presets[0]).isEmpty());
        REQUIRE (drum_kit::getSample (drums, kick).has_value());
        CHECK (drum_kit::getSample (drums, kick)->file == song.tone);
        CHECK (drum_kit::getSample (drums, kick)->gainDb == doctest::Approx (-3.0f));

        song.project->undo();
        CHECK (drum_kit::getSampledKeys (drums).empty());
        song.project->redo();
        CHECK (drum_kit::getSampledKeys (drums) == std::vector<int> { kick });
    }
}
