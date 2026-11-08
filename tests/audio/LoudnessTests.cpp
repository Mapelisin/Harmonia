#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "audio/Loudness.h"
#include "core/Project.h"

using namespace harmonia;
namespace loudness = audio::loudness;

namespace
{
    constexpr double rate = 48000.0;

    /** Stereo: a 1 kHz sine at `db` dBFS (its peak) for each of `parts` (seconds, dB). */
    juce::AudioBuffer<float> sine (std::initializer_list<std::pair<double, double>> parts)
    {
        double total = 0.0;

        for (const auto& part : parts)
            total += part.first;

        juce::AudioBuffer<float> buffer (2, (int) std::round (total * rate));
        buffer.clear();
        int position = 0;

        for (const auto& [seconds, db] : parts)
        {
            const auto amplitude = db <= -100.0 ? 0.0 : std::pow (10.0, db / 20.0);
            const auto count = (int) std::round (seconds * rate);

            for (int i = 0; i < count; ++i, ++position)
            {
                const auto value = (float) (amplitude * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * position / rate));
                buffer.setSample (0, position, value);
                buffer.setSample (1, position, value);
            }
        }

        return buffer;
    }
}

TEST_SUITE ("audio::loudness")
{
    TEST_CASE ("EBU Tech 3341: a 1 kHz sine at -23 dBFS reads -23 LUFS; silence and quiet parts are gated out")
    {
        const auto at23 = loudness::measure (sine ({ { 20.0, -23.0 } }), rate);
        REQUIRE (at23.has_value());
        CHECK (at23->lufs == doctest::Approx (-23.0).epsilon (0.1 / 23.0));
        CHECK (at23->peakDb == doctest::Approx (-23.0).epsilon (0.01));
        CHECK (at23->seconds == doctest::Approx (20.0));

        const auto at33 = loudness::measure (sine ({ { 20.0, -33.0 } }), rate);
        REQUIRE (at33.has_value());
        CHECK (at33->lufs == doctest::Approx (-33.0).epsilon (0.1 / 33.0));

        // Tech 3341 case 3: -36, -23, -36 dBFS for 10, 60, 10 s reads -23 (the quiet parts are gated out).
        const auto gated = loudness::measure (sine ({ { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 } }), rate);
        REQUIRE (gated.has_value());
        CHECK (gated->lufs == doctest::Approx (-23.0).epsilon (0.1 / 23.0));

        const auto withSilence = loudness::measure (sine ({ { 10.0, -20.0 }, { 10.0, -200.0 } }), rate);
        REQUIRE (withSilence.has_value());
        CHECK (withSilence->lufs == doctest::Approx (-20.0).epsilon (0.1 / 20.0));

        // At another sample rate too.
        juce::AudioBuffer<float> at44 (2, 441000);

        for (int i = 0; i < at44.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                at44.setSample (c, i, (float) (std::pow (10.0, -23.0 / 20.0) * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 44100.0)));

        CHECK (loudness::measure (at44, 44100.0).value().lufs == doctest::Approx (-23.0).epsilon (0.1 / 23.0));

        CHECK_FALSE (loudness::measure (sine ({ { 10.0, -200.0 } }), rate).has_value());     // silent
        CHECK_FALSE (loudness::measure (sine ({ { 0.3, -10.0 } }), rate).has_value());       // too short
    }

    TEST_CASE ("advice: up to the streaming level, as far as the peaks allow")
    {
        // Quiet with room: raise by the whole difference.
        auto advice = loudness::advise ({ -20.0, -8.0f, 120.0 });
        CHECK (advice.gainDb == doctest::Approx (6.0f));
        CHECK_FALSE (advice.peakLimited);
        CHECK (advice.text.contains ("Raise the master"));

        // Quiet, but the peaks are near the top: only to the ceiling.
        advice = loudness::advise ({ -18.0, -3.0f, 120.0 });
        CHECK (advice.gainDb == doctest::Approx (2.0f));
        CHECK (advice.peakLimited);
        CHECK (advice.text.contains ("compressor"));

        // Too loud: down.
        advice = loudness::advise ({ -9.0, -1.5f, 120.0 });
        CHECK (advice.gainDb == doctest::Approx (-5.0f));
        CHECK_FALSE (advice.peakLimited);

        // About right.
        advice = loudness::advise ({ -14.2, -2.0f, 120.0 });
        CHECK (std::abs (advice.gainDb) < 0.5f);
        CHECK (advice.text.contains ("right for streaming"));

        // At the level, but the peaks clip: down to the ceiling.
        advice = loudness::advise ({ -14.0, 0.5f, 120.0 });
        CHECK (advice.gainDb == doctest::Approx (-1.5f));
        CHECK (advice.peakLimited);
        CHECK (advice.text.contains ("crackle"));
    }

    TEST_CASE ("the song is measured as it is heard, master volume included; the advice goes onto the master fader")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        auto* clip = project->addMidiClip (track, 0.0, 16.0, "Chords");
        REQUIRE (clip != nullptr);

        for (int bar = 0; bar < 4; ++bar)
            for (int pitch : { 48, 60, 64, 67 })
                clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (bar * 4.0), te::BeatDuration::fromBeats (4.0), 50, 0, nullptr);     // soft: the peaks stay under 0 dB (louder ones would be cut off there)

        juce::String error;
        const auto before = loudness::measureSong (project->getEdit(), error);
        REQUIRE_MESSAGE (before.has_value(), error.toStdString());
        CHECK (before->lufs < -3.0);
        CHECK (before->lufs > -50.0);
        CHECK (before->peakDb < -0.5f);
        CHECK (before->seconds == doctest::Approx (8.0 + 2.0).epsilon (0.02));     // with the export's tail

        // 6 dB down on the master: 6 LU quieter.
        CHECK (loudness::addToMasterVolume (project->getEdit(), -6.0f) == doctest::Approx (-6.0f).epsilon (0.01));
        const auto after = loudness::measureSong (project->getEdit(), error);
        REQUIRE (after.has_value());
        CHECK (after->lufs - before->lufs == doctest::Approx (-6.0).epsilon (0.05));
        CHECK (after->peakDb < before->peakDb - 4.5f);     // the highest sample shifts a little from render to render (the synth's phases)

        // The fader goes up to +6 dB: only what fits is added.
        const auto fader = project->getEdit().getMasterVolumePlugin()->getVolumeDb();
        CHECK (loudness::addToMasterVolume (project->getEdit(), 20.0f) == doctest::Approx (6.0f - fader).epsilon (0.01));
        CHECK (project->getEdit().getMasterVolumePlugin()->getVolumeDb() == doctest::Approx (6.0f).epsilon (0.01));

        // Nothing to hear: says so.
        auto empty = core::Project::createEmpty (test::session());
        CHECK_FALSE (loudness::measureSong (empty->getEdit(), error).has_value());
        CHECK (error.contains ("Nothing to export"));
    }
}
