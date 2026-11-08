#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Pump.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace pump = audio::pump;
namespace effects = audio::effects;

namespace
{
    constexpr double rate = 48000.0;

    /** A steady level of 1 through the pump in blocks of 512 samples, at 120 BPM from beat 0. */
    std::vector<float> run (double beats, double stepBeats, float depth, float length, bool playing = true)
    {
        pump::Processor processor;
        processor.prepare (rate);
        const auto beatsPerSample = 2.0 / rate;
        const auto numSamples = (int) (beats / beatsPerSample);
        juce::AudioBuffer<float> buffer (2, numSamples);

        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (c), 1.0f, numSamples);

        for (int start = 0; start < numSamples; start += 512)
        {
            float* channels[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (1, start) };
            processor.process (channels, 2, std::min (512, numSamples - start), playing, start * beatsPerSample, beatsPerSample,
                               stepBeats, depth, length);
        }

        CHECK (buffer.getSample (1, numSamples / 3) == buffer.getSample (0, numSamples / 3));
        return { buffer.getReadPointer (0), buffer.getReadPointer (0) + numSamples };
    }

    float at (const std::vector<float>& levels, double beat)
    {
        return levels[(size_t) (beat * rate / 2.0)];
    }

    const effects::EffectType& pumpType()
    {
        static const auto type = []
        {
            for (const auto& t : effects::getBuiltIn())
                if (t.name == "Pump")
                    return t;

            return effects::EffectType {};
        }();

        return type;
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
}

TEST_SUITE ("audio::pump")
{
    TEST_CASE ("the shape of a duck: down at once, held a moment, swelling back by its length")
    {
        CHECK (pump::gainAt (0.0, 0.7f, 0.6f) == doctest::Approx (0.3f));
        CHECK (pump::gainAt (0.6, 0.7f, 0.6f) == doctest::Approx (1.0f));
        CHECK (pump::gainAt (0.9, 0.7f, 0.6f) == doctest::Approx (1.0f));
        CHECK (pump::gainAt (0.3, 0.7f, 0.6f) == doctest::Approx (0.65f));       // half way: half back
        CHECK (pump::gainAt (0.03, 0.7f, 0.6f) < 0.32f);                          // held low at first
        CHECK (pump::gainAt (0.3, 0.0f, 0.6f) == doctest::Approx (1.0f));        // no depth, no duck

        auto previous = 0.0f;

        for (double phase = 0.0; phase < 1.0; phase += 0.01)
        {
            const auto gain = pump::gainAt (phase, 1.0f, 0.5f);
            CHECK (gain >= previous);
            CHECK (gain >= 0.0f);
            CHECK (gain <= 1.0f);
            previous = gain;
        }

        CHECK (pump::getName (pump::Rate::quarter) == "1/4");
        CHECK (pump::getName (pump::Rate::bar) == "1 bar");
    }

    TEST_CASE ("on every beat the level ducks and comes back, without a click")
    {
        const auto levels = run (4.0, 1.0, 0.7f, 0.6f);

        for (int beat = 0; beat < 4; ++beat)
        {
            CAPTURE (beat);
            CHECK (at (levels, beat + 0.02) == doctest::Approx (0.3f).epsilon (0.02));
            CHECK (at (levels, beat + 0.3) == doctest::Approx (0.65f).epsilon (0.02));
            CHECK (at (levels, beat + 0.8) == doctest::Approx (1.0f).epsilon (0.001));
        }

        // Down within a few milliseconds, but never all at once.
        auto biggestJump = 0.0f;

        for (size_t i = 1; i < levels.size(); ++i)
            biggestJump = std::max (biggestJump, std::abs (levels[i] - levels[i - 1]));

        CHECK (biggestJump < 0.02f);
        CHECK (biggestJump > 0.005f);
    }

    TEST_CASE ("eighths duck twice a beat; stopped, it doesn't duck at all")
    {
        const auto levels = run (2.0, 0.5, 0.5f, 0.8f);
        int ducks = 0;

        for (size_t i = 1; i < levels.size(); ++i)
            if (levels[i - 1] > 0.75f && levels[i] <= 0.75f)
                ++ducks;

        CHECK (ducks == 4);

        for (auto level : run (1.0, 1.0, 0.7f, 0.6f, false))
            CHECK (level == doctest::Approx (1.0f));
    }

    TEST_CASE ("the pump is an effect like the others, saved with the project")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        REQUIRE (pumpType().isBuiltIn());

        auto* track = project->getTracks().getFirst();
        juce::String error;
        auto* plugin = dynamic_cast<pump::Plugin*> (effects::add (track->pluginList, pumpType(), error));
        REQUIRE_MESSAGE (plugin != nullptr, error.toStdString());
        CHECK (effects::getDisplayName (*plugin) == "Pump");

        juce::StringArray names;

        for (auto* parameter : plugin->getAutomatableParameters())
            names.add (parameter->getParameterName());

        CHECK (names.joinIntoString (", ") == "Depth, Rate, Length");
        CHECK (plugin->depth->getCurrentValueAsString() == "70%");
        CHECK (plugin->rate->getCurrentValueAsString() == "1/4");
        CHECK (plugin->getRate() == pump::Rate::quarter);

        plugin->rate->setParameter (0.0f, juce::sendNotificationSync);
        plugin->depth->setParameter (40.0f, juce::sendNotificationSync);
        CHECK (plugin->rate->getCurrentValueAsString() == "1/8");

        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

        pump::Plugin* again = nullptr;

        for (auto* p : effects::get (loaded->getTracks().getFirst()->pluginList))
            if (auto* found = dynamic_cast<pump::Plugin*> (p))
                again = found;

        REQUIRE (again != nullptr);
        CHECK (again->getRate() == pump::Rate::eighth);
        CHECK (again->depth->getCurrentValue() == doctest::Approx (40.0f));
        CHECK (again->length->getCurrentValue() == doctest::Approx (pump::defaultLength));
    }

    TEST_CASE ("in the exported song a steady tone ducks on the beats, in time with the tempo")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->addTrack ("Pad");
        REQUIRE (track != nullptr);
        core::tracks::setKind (*track, core::tracks::Kind::audio);
        juce::String error;
        REQUIRE (project->addAudioClip (*track, test::writeTone (temp.getFile ("Tone.wav"), 4.0, 0.5f, 2), 0.0, error) != nullptr);

        auto* plugin = dynamic_cast<pump::Plugin*> (effects::add (track->pluginList, pumpType(), error));
        REQUIRE (plugin != nullptr);
        plugin->depth->setParameter (80.0f, juce::sendNotificationSync);

        const auto file = temp.getFile ("Song.wav");
        REQUIRE (audio::exportWav (project->getEdit(), file).isEmpty());

        // 120 BPM: a beat every half second. Just after each beat: quiet; late in it: full.
        for (int beat = 1; beat < 6; ++beat)
        {
            CAPTURE (beat);
            const auto start = beat * 0.5;
            const auto low = rmsOf (file, start + 0.01, start + 0.05);
            const auto high = rmsOf (file, start + 0.40, start + 0.48);
            CHECK (low < high * 0.35f);
            CHECK (high > 0.1f);
        }
    }
}
