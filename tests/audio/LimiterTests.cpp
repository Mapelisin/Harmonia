#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Limiter.h"
#include "audio/Loudness.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace limiter = audio::limiter;
namespace loudness = audio::loudness;
namespace effects = audio::effects;

namespace
{
    constexpr double rate = 48000.0;

    juce::AudioBuffer<float> sine (double hz, float level, double seconds)
    {
        juce::AudioBuffer<float> buffer (2, (int) (rate * seconds));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto x = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate);
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }

        return buffer;
    }

    float peakOf (const juce::AudioBuffer<float>& buffer)
    {
        return std::max (buffer.getMagnitude (0, 0, buffer.getNumSamples()), buffer.getMagnitude (1, 0, buffer.getNumSamples()));
    }

    /** Through the limiter in blocks of 512 samples, as the engine does it. */
    void run (limiter::Processor& processor, juce::AudioBuffer<float>& buffer, float gainDb, float ceilingDb = -1.0f)
    {
        for (int start = 0; start < buffer.getNumSamples(); start += 512)
        {
            float* channels[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (1, start) };
            processor.process (channels, 2, std::min (512, buffer.getNumSamples() - start), gainDb, ceilingDb,
                               limiter::defaultReleaseMs);
        }
    }

    limiter::Plugin* findLimiter (te::Edit& edit)
    {
        for (auto* plugin : effects::get (edit.getMasterPluginList()))
            if (auto* found = dynamic_cast<limiter::Plugin*> (plugin))
                return found;

        return nullptr;
    }

    const effects::EffectType& limiterType()
    {
        static const auto type = []
        {
            for (const auto& t : effects::getBuiltIn())
                if (t.name == "Limiter")
                    return t;

            return effects::EffectType {};
        }();

        return type;
    }

    /** Quiet and peaky: a 220 Hz sine at 0.1, and every half second a click (1 ms of 2 kHz) at 0.9. */
    juce::AudioBuffer<float> peaky (double seconds)
    {
        auto buffer = sine (220.0, 0.1f, seconds);
        const auto clickLength = (int) (rate / 1000.0);

        for (int start = 0; start < buffer.getNumSamples(); start += (int) (rate / 2.0))
            for (int i = 0; i < clickLength && start + i < buffer.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                    buffer.setSample (c, start + i, 0.9f * (float) std::sin (juce::MathConstants<double>::twoPi * 2000.0 * i / rate));

        return buffer;
    }

    juce::File writePeaky (const juce::File& file, double seconds)
    {
        const auto buffer = peaky (seconds);

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
        return file;
    }
}

TEST_SUITE ("audio::limiter")
{
    TEST_CASE ("under the ceiling the sound only comes out later, unchanged")
    {
        limiter::Processor processor;
        processor.prepare (rate, 2);
        const auto latency = processor.getLatencySamples();
        CHECK (latency == 95);      // 2 ms at 48 kHz

        const auto input = sine (1000.0, 0.5f, 1.0);
        auto output = input;
        run (processor, output, 0.0f);

        float worst = 0.0f;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i + latency < input.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (output.getSample (c, i + latency) - input.getSample (c, i)));

        CHECK (worst < 1.0e-6f);
        CHECK (output.getMagnitude (0, 0, latency) == 0.0f);
        CHECK (processor.getReductionDb() == doctest::Approx (0.0f));
    }

    TEST_CASE ("raised over the ceiling, the peaks stay under it and the sound gets louder")
    {
        limiter::Processor processor;
        processor.prepare (rate, 2);
        auto sound = peaky (2.0);
        const auto quietRms = sound.getRMSLevel (0, 0, sound.getNumSamples());
        CHECK (peakOf (sound) > 0.85f);

        run (processor, sound, 12.0f, -1.0f);
        CHECK (peakOf (sound) <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-6f);
        CHECK (sound.getRMSLevel (0, 0, sound.getNumSamples()) > quietRms * 2.5f);    // 12 dB is 4 times, less what the clicks take
        CHECK (processor.getReductionDb() <= 0.0f);

        // A steady sound over the ceiling is held at it.
        auto loud = sine (100.0, 0.9f, 2.0);
        run (processor, loud, 12.0f, -1.0f);
        CHECK (peakOf (loud) <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-6f);
        CHECK (peakOf (loud) > juce::Decibels::decibelsToGain (-1.2f));
        CHECK (processor.getReductionDb() < -10.0f);
    }

    TEST_CASE ("a sudden peak is caught ahead, the level going down and back up smoothly")
    {
        limiter::Processor processor;
        processor.prepare (rate, 2);
        const auto latency = processor.getLatencySamples();

        juce::AudioBuffer<float> buffer (2, (int) rate * 2);
        buffer.clear();
        const auto spike = (int) rate / 2;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (c, i, i == spike ? 1.0f : 0.1f);

        run (processor, buffer, 0.0f, -1.0f);
        const auto ceiling = juce::Decibels::decibelsToGain (-1.0f);
        const auto* out = buffer.getReadPointer (0);

        CHECK (out[spike + latency] <= ceiling + 1.0e-6f);
        CHECK (out[spike + latency] > ceiling - 0.01f);         // turned down only as far as needed
        CHECK (out[spike + latency - latency / 2] < 0.0975f);   // the level is already on its way down
        CHECK (out[spike - 1] == doctest::Approx (0.1f));       // ...but not before the look-ahead

        // No step anywhere but the peak itself (which a hard clip would make).
        float biggestStep = 0.0f;

        for (int i = latency + 1; i < buffer.getNumSamples(); ++i)
            if (i != spike + latency && i != spike + latency + 1)
                biggestStep = std::max (biggestStep, std::abs (out[i] - out[i - 1]));

        CHECK (biggestStep < 0.001f);

        // A second later it is all the way back up.
        CHECK (out[spike + latency + (int) rate] == doctest::Approx (0.1f).epsilon (0.001));
    }

    TEST_CASE ("the limiter is an effect like the others, saved with the project")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        REQUIRE (limiterType().isBuiltIn());

        juce::String error;
        auto* plugin = effects::add (project->getEdit().getMasterPluginList(), limiterType(), error);
        REQUIRE_MESSAGE (plugin != nullptr, error.toStdString());
        auto* added = dynamic_cast<limiter::Plugin*> (plugin);
        REQUIRE (added != nullptr);
        CHECK (effects::getDisplayName (*plugin) == "Limiter");

        juce::StringArray names;

        for (auto* parameter : plugin->getAutomatableParameters())
            names.add (parameter->getParameterName());

        CHECK (names.joinIntoString (", ") == "Gain, Ceiling, Release");
        CHECK (added->ceiling->getCurrentValue() == doctest::Approx (-1.0f));
        CHECK (added->gain->getCurrentValueAsString() == "0.0 dB");

        added->gain->setParameter (6.0f, juce::sendNotificationSync);
        added->ceiling->setParameter (-2.0f, juce::sendNotificationSync);
        CHECK (added->gain->getCurrentValueAsString() == "+6.0 dB");

        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

        auto* again = findLimiter (loaded->getEdit());
        REQUIRE (again != nullptr);
        CHECK (again->gain->getCurrentValue() == doctest::Approx (6.0f));
        CHECK (again->ceiling->getCurrentValue() == doctest::Approx (-2.0f));
        CHECK (again->release->getCurrentValue() == doctest::Approx (limiter::defaultReleaseMs));
    }

    TEST_CASE ("on the master it keeps the exported song's peaks under the ceiling")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->addTrack ("Peaks");
        REQUIRE (track != nullptr);
        core::tracks::setKind (*track, core::tracks::Kind::audio);
        juce::String error;
        REQUIRE (project->addAudioClip (*track, writePeaky (temp.getFile ("Peaky.wav"), 4.0), 0.0, error) != nullptr);
        project->getEdit().getMasterVolumePlugin()->setVolumeDb (0.0f);

        auto* plugin = dynamic_cast<limiter::Plugin*> (effects::add (project->getEdit().getMasterPluginList(), limiterType(), error));
        REQUIRE (plugin != nullptr);
        plugin->gain->setParameter (10.0f, juce::sendNotificationSync);

        const auto measured = loudness::measureSong (project->getEdit(), error);
        REQUIRE_MESSAGE (measured.has_value(), error.toStdString());
        CHECK (measured->peakDb < -0.95f);
        CHECK (measured->peakDb > -1.5f);

        // Without it: quieter, and the clicks go well over the ceiling.
        effects::setBypassed (*plugin, true);
        const auto without = loudness::measureSong (project->getEdit(), error);
        REQUIRE (without.has_value());
        CHECK (measured->lufs - without->lufs > 7.0);
        CHECK (without->peakDb > -1.5f);
    }

    TEST_CASE ("a quiet song with high peaks is brought to the streaming level by the limiter, in one undo step")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->addTrack ("Peaks");
        REQUIRE (track != nullptr);
        core::tracks::setKind (*track, core::tracks::Kind::audio);
        juce::String error;
        REQUIRE (project->addAudioClip (*track, writePeaky (temp.getFile ("Peaky.wav"), 8.0), 0.0, error) != nullptr);
        project->clearUndoHistory();

        const auto faderBefore = project->getEdit().getMasterVolumePlugin()->getVolumeDb();
        const auto before = loudness::measureSong (project->getEdit(), error);
        REQUIRE (before.has_value());
        const auto advice = loudness::advise (*before);
        CHECK (advice.peakLimited);
        CHECK (advice.text.contains ("limiter"));

        std::vector<juce::String> steps;
        project->beginGesture ("Limiter for streaming");
        const auto result = loudness::raiseWithLimiter (project->getEdit(), error, loudness::streamingLufs,
                                                        [&steps] (const juce::String& step) { steps.push_back (step); });
        REQUIRE_MESSAGE (result.has_value(), error.toStdString());

        CHECK (result->before.lufs == doctest::Approx (before->lufs).epsilon (0.01));
        CHECK (result->after.lufs == doctest::Approx (loudness::streamingLufs).epsilon (0.04));
        CHECK (result->after.peakDb < -0.95f);
        CHECK (result->gainDb > 5.0f);
        REQUIRE (! steps.empty());
        CHECK (steps.front() == "Measuring the song...");

        // The limiter is the master's last effect; the fader is at 0 dB, so its ceiling holds.
        auto* added = findLimiter (project->getEdit());
        REQUIRE (added != nullptr);
        CHECK (effects::get (project->getEdit().getMasterPluginList()).getLast() == added);
        CHECK (added->gain->getCurrentValue() == doctest::Approx (result->gainDb));
        CHECK (project->getEdit().getMasterVolumePlugin()->getVolumeDb() == doctest::Approx (0.0f));

        // Once again: the limiter is already there, nothing more is added; the level stays.
        project->beginGesture ("Limiter for streaming");
        const auto again = loudness::raiseWithLimiter (project->getEdit(), error);
        REQUIRE (again.has_value());
        CHECK (effects::get (project->getEdit().getMasterPluginList()).size() == 1);
        CHECK (again->after.lufs == doctest::Approx (loudness::streamingLufs).epsilon (0.04));

        // Undone, step by step: as it was.
        project->undo();
        project->undo();
        CHECK (findLimiter (project->getEdit()) == nullptr);
        CHECK (project->getEdit().getMasterVolumePlugin()->getVolumeDb() == doctest::Approx (faderBefore));
    }
}
