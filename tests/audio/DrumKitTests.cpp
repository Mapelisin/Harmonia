#include <doctest.h>

#include "TestAudioRig.h"
#include "audio/DrumKit.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Transport.h"
#include "core/NoteEditor.h"
#include "core/Project.h"

using namespace harmonia;
namespace drum_kit = audio::drum_kit;
namespace instruments = audio::instruments;
using drum_kit::Kit;
using drum_kit::Sound;

namespace
{
    constexpr double rate = 48000.0;

    struct Hit
    {
        double seconds;
        int note;
        float velocity = 1.0f;
    };

    struct Settings
    {
        Kit kit = Kit::punchy;
        float tune = 0.0f, decay = 1.0f;
    };

    struct Rendered
    {
        std::vector<float> left, right;
        int playingAtEnd = 0;

        /** The loudest sample of the left side between two times. */
        float peak (double from, double to) const
        {
            auto result = 0.0f;

            for (auto i = index (from); i < index (to); ++i)
                result = std::max (result, std::abs (left[i]));

            return result;
        }

        float rms (double from, double to, bool rightSide = false) const
        {
            const auto& side = rightSide ? right : left;
            double sum = 0.0;
            const auto first = index (from), last = index (to);

            for (auto i = first; i < last; ++i)
                sum += (double) side[i] * side[i];

            return last > first ? (float) std::sqrt (sum / (double) (last - first)) : 0.0f;
        }

        /** How many times a second the sound crosses zero: low for a kick, high for a hat. */
        double crossingsPerSecond (double from, double to) const
        {
            int crossings = 0;

            for (auto i = index (from) + 1; i < index (to); ++i)
                if ((left[i - 1] < 0.0f) != (left[i] < 0.0f))
                    ++crossings;

            return crossings / (to - from);
        }

        size_t index (double seconds) const     { return std::min (left.size(), (size_t) (seconds * rate)); }
    };

    /** The hits through the kit, `seconds` of it, in blocks of 256 samples (each hit exactly where it falls). */
    Rendered play (std::vector<Hit> hits, double seconds, Settings settings = {})
    {
        drum_kit::Processor kit;
        kit.prepare (rate);

        Rendered out;
        const auto numSamples = (int) (seconds * rate);
        out.left.assign ((size_t) numSamples, 0.0f);
        out.right.assign ((size_t) numSamples, 0.0f);

        std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.seconds < b.seconds; });
        size_t next = 0;
        auto done = 0;

        while (done < numSamples)
        {
            auto end = std::min (numSamples, done + 256);

            if (next < hits.size())
                end = std::min (end, std::max (done, (int) (hits[next].seconds * rate)));

            kit.render (out.left.data() + done, out.right.data() + done, end - done);
            done = end;

            while (next < hits.size() && (int) (hits[next].seconds * rate) <= done)
            {
                kit.noteOn (hits[next].note, hits[next].velocity, settings.kit, settings.tune, settings.decay);
                ++next;
            }
        }

        out.playingAtEnd = kit.getNumPlaying();
        return out;
    }

    Rendered hit (int note, double seconds = 1.0, Settings settings = {}, float velocity = 1.0f)
    {
        return play ({ { 0.0, note, velocity } }, seconds, settings);
    }

    /** The RMS of a stretch of a WAV file, from `start` to `end` seconds. */
    float rmsOf (const juce::File& file, double start, double end)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        REQUIRE (reader != nullptr);
        const auto first = (juce::int64) (start * reader->sampleRate);
        const auto count = (int) ((end - start) * reader->sampleRate);
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, count);
        reader->read (&buffer, 0, count, first, true, true);
        return buffer.getRMSLevel (0, 0, count);
    }

    /** A kick on each of the first `beats` beats of a new clip on the track. */
    void addKicks (core::Project& project, te::AudioTrack& track, int beats)
    {
        auto* clip = project.addMidiClip (track, 0.0, (double) beats, "Beat");
        REQUIRE (clip != nullptr);
        std::vector<core::Note> notes;

        for (int beat = 0; beat < beats; ++beat)
        {
            core::Note n;
            n.pitch = 36;
            n.startBeat = beat;
            n.lengthBeats = 0.25;
            n.velocity = 110;
            notes.push_back (n);
        }

        core::NoteEditor (*clip).add (notes);
    }
}

TEST_SUITE ("audio::drum_kit")
{
    TEST_CASE ("the General MIDI keys: kick, snare, hats, toms, cymbals; the other keys are silent")
    {
        CHECK (drum_kit::getSound (36) == Sound::kick);
        CHECK (drum_kit::getSound (35) == Sound::kick);
        CHECK (drum_kit::getSound (38) == Sound::snare);
        CHECK (drum_kit::getSound (39) == Sound::clap);
        CHECK (drum_kit::getSound (42) == Sound::closedHat);
        CHECK (drum_kit::getSound (46) == Sound::openHat);
        CHECK (drum_kit::getSound (45) == Sound::tom);
        CHECK (drum_kit::getSound (49) == Sound::crash);
        CHECK (drum_kit::getSound (51) == Sound::ride);
        CHECK (drum_kit::getSound (56) == Sound::cowbell);
        CHECK (drum_kit::getSound (63) == Sound::hand);
        CHECK (drum_kit::getSound (34) == Sound::none);
        CHECK (drum_kit::getSound (82) == Sound::none);
        CHECK (drum_kit::getName (Kit::punchy) == "Punchy");
        CHECK (drum_kit::getName (Kit::boomy) == "808");
        CHECK (drum_kit::getName (Kit::soft) == "Lo-fi");
        CHECK (drum_kit::getKits().size() == 3);

        for (auto note : { 20, 34, 82, 100 })
        {
            CAPTURE (note);
            const auto out = hit (note, 0.2);
            CHECK (out.peak (0.0, 0.2) == 0.0f);
            CHECK (out.playingAtEnd == 0);
        }

        CHECK (hit (36, 0.2, {}, 0.0f).peak (0.0, 0.2) == 0.0f);       // velocity 0 is no hit
    }

    TEST_CASE ("a kick is low, a snare higher, a hat bright; each is loud enough and none clips")
    {
        const auto kick = hit (36, 0.3), snare = hit (38, 0.3), hat = hit (42, 0.3), clap = hit (39, 0.3);
        const auto kickRate = kick.crossingsPerSecond (0.01, 0.2);
        const auto snareRate = snare.crossingsPerSecond (0.0, 0.1);
        const auto hatRate = hat.crossingsPerSecond (0.0, 0.04);
        CAPTURE (kickRate);
        CAPTURE (snareRate);
        CAPTURE (hatRate);
        CHECK (kickRate < 400.0);
        CHECK (kickRate < snareRate);
        CHECK (snareRate < hatRate);
        CHECK (hatRate > 5000.0);

        CHECK (kick.peak (0.0, 0.3) > 0.4f);
        CHECK (kick.peak (0.0, 0.3) <= 1.0f);
        CHECK (snare.peak (0.0, 0.3) > 0.3f);
        CHECK (snare.peak (0.0, 0.3) <= 1.0f);
        CHECK (clap.peak (0.0, 0.3) > 0.1f);
        CHECK (hat.peak (0.0, 0.3) > 0.05f);
        CHECK (hat.peak (0.0, 0.3) < kick.peak (0.0, 0.3));

        // The kick falls in pitch: faster at first than later on.
        CHECK (kick.crossingsPerSecond (0.0, 0.02) > kick.crossingsPerSecond (0.1, 0.2));
    }

    TEST_CASE ("every hit rings out by itself and then stops; the 808 kick rings long, a crash longer still")
    {
        for (auto note : { 35, 36, 37, 38, 39, 42, 44, 46, 41, 50, 49, 51, 53, 54, 56, 70, 75, 62, 81 })
        {
            CAPTURE (note);
            const auto out = hit (note, 4.0);
            CHECK (out.peak (0.0, 0.5) > 0.02f);
            CHECK (out.peak (0.0, 4.0) <= 1.0f);
            CHECK (out.playingAtEnd == 0);
            CHECK (out.peak (3.5, 4.0) == 0.0f);
        }

        const auto punchy = hit (36, 1.0), boomy = hit (36, 1.0, { Kit::boomy });
        CHECK (boomy.rms (0.6, 0.8) > 4.0f * punchy.rms (0.6, 0.8));
        CHECK (hit (49, 2.0).rms (0.8, 1.0) > hit (42, 2.0).rms (0.8, 1.0) * 100.0f);

        // Lo-fi: the top rolled off, so a hat crosses zero less often.
        CHECK (hit (42, 0.3, { Kit::soft }).crossingsPerSecond (0.0, 0.04) < hit (42, 0.3).crossingsPerSecond (0.0, 0.04));
    }

    TEST_CASE ("harder hits are louder; Tune moves the pitch, Decay the length")
    {
        const auto full = hit (38, 0.5).peak (0.0, 0.5);
        const auto half = hit (38, 0.5, {}, 0.5f).peak (0.0, 0.5);
        const auto quarter = hit (38, 0.5, {}, 0.25f).peak (0.0, 0.5);
        CHECK (half < 0.5f * full);
        CHECK (half > 0.25f * full);
        CHECK (quarter < 0.5f * half);

        // An octave up: the kick's tone crosses zero about twice as often.
        const auto low = hit (36, 0.4).crossingsPerSecond (0.1, 0.3);
        const auto high = hit (36, 0.4, { Kit::punchy, 12.0f }).crossingsPerSecond (0.1, 0.3);
        CHECK (high / low == doctest::Approx (2.0).epsilon (0.25));

        const auto shortSnare = hit (38, 1.0, { Kit::punchy, 0.0f, 0.5f });
        const auto longSnare = hit (38, 1.0, { Kit::punchy, 0.0f, 2.0f });
        CHECK (longSnare.rms (0.15, 0.3) > 10.0f * shortSnare.rms (0.15, 0.3));
    }

    TEST_CASE ("a closed hat stops the open one; a drum hit again stops ringing; at most 32 at once")
    {
        const auto open = play ({ { 0.0, 46 } }, 0.3);
        const auto choked = play ({ { 0.0, 46 }, { 0.05, 42 } }, 0.3);
        CHECK (open.rms (0.15, 0.25) > 0.001f);
        CHECK (choked.rms (0.15, 0.25) < 0.1f * open.rms (0.15, 0.25));

        CHECK (play ({ { 0.0, 49 } }, 0.52).playingAtEnd == 1);
        CHECK (play ({ { 0.0, 49 }, { 0.5, 49 } }, 0.52).playingAtEnd == 1);
        CHECK (play ({ { 0.0, 49 }, { 0.5, 57 } }, 0.52).playingAtEnd == 2);

        std::vector<Hit> many;

        for (int note = 35; note < 75; ++note)
            many.push_back ({ 0.0, note });

        const auto out = play (many, 0.01);
        CHECK (out.playingAtEnd == drum_kit::Processor::maxVoices);

        for (auto sample : out.left)
            REQUIRE (std::isfinite (sample));
    }

    TEST_CASE ("the same hits sound the same every time; hats to the right, the kick in the middle")
    {
        const std::vector<Hit> groove { { 0.0, 36 }, { 0.0, 42 }, { 0.25, 42 }, { 0.5, 38 }, { 0.5, 42 }, { 0.75, 46 } };
        const auto a = play (groove, 1.0), b = play (groove, 1.0);
        CHECK (a.left == b.left);
        CHECK (a.right == b.right);

        const auto kick = hit (36, 0.3), hat = hit (42, 0.3);
        CHECK (kick.rms (0.0, 0.3) == doctest::Approx (kick.rms (0.0, 0.3, true)));
        CHECK (hat.rms (0.0, 0.1, true) > 1.2f * hat.rms (0.0, 0.1));
    }

    TEST_CASE ("a groove on each kit, to listen to")
    {
        // Two bars at 100 BPM: kick, snare, hats, a clap, a tom fill and a crash; each kit in turn.
        constexpr double beat = 0.6;
        std::vector<Hit> groove;

        for (int bar = 0; bar < 2; ++bar)
        {
            const auto start = bar * 4 * beat;

            for (int eighth = 0; eighth < 8; ++eighth)
                groove.push_back ({ start + eighth * beat / 2, eighth == 7 && bar == 0 ? 46 : 42, eighth % 2 == 0 ? 0.8f : 0.55f });

            for (auto kick : { 0.0, 1.5, 2.0 })
                groove.push_back ({ start + kick * beat, 36 });

            groove.push_back ({ start + beat, 38 });
            groove.push_back ({ start + 3 * beat, bar == 0 ? 38 : 39 });
        }

        for (auto [at, note] : { std::pair { 3.5, 48 }, { 3.625, 47 }, { 3.75, 45 }, { 3.875, 43 } })
            groove.push_back ({ 4 * beat + at * beat, note, 0.9f });

        groove.push_back ({ 8 * beat, 49 });
        groove.push_back ({ 8 * beat, 36 });

        const auto length = 8 * beat + 1.5;
        std::vector<float> left, right;

        for (auto kit : drum_kit::getKits())
        {
            const auto out = play (groove, length, { kit });
            CHECK (out.peak (0.0, length) <= 1.0f);
            left.insert (left.end(), out.left.begin(), out.left.end());
            right.insert (right.end(), out.right.begin(), out.right.end());
        }

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            const auto file = juce::File (dir).getChildFile ("drum-kits.wav");
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                           .withSampleRate (rate)
                                                           .withNumChannels (2)
                                                           .withBitsPerSample (24));
            REQUIRE (writer != nullptr);
            const float* channels[] = { left.data(), right.data() };
            CHECK (writer->writeFromFloatArrays (channels, 2, (int) left.size()));
        }
    }

    TEST_CASE ("the drum machine is a track's instrument: Kit, Tune and Decay, saved with the project")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->getTracks().getFirst();
        REQUIRE (instruments::useBuiltInSynth (*track).isEmpty());
        CHECK_FALSE (instruments::playsDrumKit (*track));

        REQUIRE (instruments::useDrumKit (*track).isEmpty());
        CHECK (instruments::playsDrumKit (*track));
        CHECK (instruments::getName (*track) == "Drums");
        CHECK (audio::effects::get (track->pluginList).isEmpty());     // an instrument, not an effect

        auto* plugin = dynamic_cast<drum_kit::Plugin*> (instruments::get (*track));
        REQUIRE (plugin != nullptr);
        CHECK (plugin->isSynth());

        juce::StringArray names;

        for (auto* parameter : plugin->getAutomatableParameters())
            names.add (parameter->getParameterName());

        CHECK (names.joinIntoString (", ") == "Kit, Tune, Decay");
        CHECK (plugin->kit->getCurrentValueAsString() == "Punchy");
        CHECK (plugin->tune->getCurrentValueAsString() == "0 st");
        CHECK (plugin->decay->getCurrentValueAsString() == "100%");

        plugin->kit->setParameter (1.0f, juce::sendNotificationSync);
        plugin->tune->setParameter (3.0f, juce::sendNotificationSync);
        plugin->decay->setParameter (150.0f, juce::sendNotificationSync);
        CHECK (plugin->getKit() == Kit::boomy);
        CHECK (plugin->kit->getCurrentValueAsString() == "808");
        CHECK (plugin->tune->getCurrentValueAsString() == "+3 st");

        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

        auto* again = dynamic_cast<drum_kit::Plugin*> (instruments::get (*loaded->getTracks().getFirst()));
        REQUIRE (again != nullptr);
        CHECK (again->getKit() == Kit::boomy);
        CHECK (again->tune->getCurrentValue() == doctest::Approx (3.0f));
        CHECK (again->decay->getCurrentValue() == doctest::Approx (150.0f));

        // And back to the synth.
        REQUIRE (instruments::useBuiltInSynth (*track).isEmpty());
        CHECK_FALSE (instruments::playsDrumKit (*track));
    }

    TEST_CASE ("in the exported song the kicks hit on the beats")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->getTracks().getFirst();
        REQUIRE (instruments::useDrumKit (*track).isEmpty());
        addKicks (*project, *track, 8);

        const auto file = temp.getFile ("Song.wav");
        REQUIRE (audio::exportWav (project->getEdit(), file).isEmpty());

        // 120 BPM: a beat every half second. Right after it: the kick; just before the next: quiet.
        for (int beat = 0; beat < 7; ++beat)
        {
            CAPTURE (beat);
            const auto loud = rmsOf (file, beat * 0.5 + 0.005, beat * 0.5 + 0.1);
            const auto quiet = rmsOf (file, beat * 0.5 + 0.4, beat * 0.5 + 0.49);
            CHECK (loud > 0.05f);
            CHECK (loud > 10.0f * quiet);
        }
    }

    TEST_CASE ("playing the song, the drums sound through the audio interface")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto* track = rig.project->getTracks().getFirst();
        REQUIRE (instruments::useDrumKit (*track).isEmpty());
        addKicks (*rig.project, *track, 8);
        edit.dispatchPendingUpdatesSynchronously();

        audio::Transport transport (edit);
        CHECK (rig.process (0.3, 0.0f) < 1.0e-4f);      // stopped: silence
        transport.play();
        rig.process (0.05, 0.0f);
        CHECK (rig.process (1.0, 0.0f) > 0.02f);
        transport.stop();
    }
}
