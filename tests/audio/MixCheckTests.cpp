#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "audio/Loudness.h"
#include "audio/MixCheck.h"
#include "core/Project.h"

using namespace harmonia;
namespace mix_check = audio::mix_check;
using audio::loudness::Measurement;

namespace
{
    constexpr double rate = 48000.0;

    /** Two seconds of a sine at `hz`, the same left and right (or the right one upside down). */
    juce::AudioBuffer<float> sine (double hz, bool opposed = false)
    {
        juce::AudioBuffer<float> buffer (2, (int) (rate * 2.0));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto x = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate);
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, opposed ? -x : x);
        }

        return buffer;
    }

    mix_check::Balance balanceOf (const juce::AudioBuffer<float>& buffer)
    {
        mix_check::BalanceMeter meter (rate);
        meter.process (buffer, buffer.getNumSamples());
        return meter.getBalance();
    }

    /** A mix with nothing wrong: streaming level, room under 0 dB, normal stereo, bass and top. */
    mix_check::Report goodMix()
    {
        mix_check::Report report;
        report.mix = { -14.0, -1.0f, 180.0 };
        report.balance = { 0.7, 2.0, -20.0 };
        report.tracks = { { "Drums", Measurement { -16.0, -3.0f, 180.0 } },
                          { "Bass", Measurement { -18.0, -6.0f, 180.0 } },
                          { "Pad", Measurement { -25.0, -12.0f, 180.0 } },
                          { "Unused", std::nullopt } };
        return report;
    }

    juce::String problemsOf (const mix_check::Report& report)
    {
        return mix_check::findProblems (report).joinIntoString ("\n");
    }
}

TEST_SUITE ("audio::mix_check")
{
    TEST_CASE ("the bass, the top and the stereo are measured")
    {
        const auto bass = balanceOf (sine (50.0));
        CHECK (bass.lowDb > 20.0);
        CHECK (bass.highDb < -20.0);
        CHECK (bass.correlation == doctest::Approx (1.0).epsilon (0.001));

        const auto middle = balanceOf (sine (1000.0));
        CHECK (middle.lowDb < -20.0);
        CHECK (middle.highDb < -20.0);

        const auto top = balanceOf (sine (15000.0));
        CHECK (top.highDb > 20.0);
        CHECK (top.lowDb < -20.0);

        // Left upside down from right: opposed.
        CHECK (balanceOf (sine (1000.0, true)).correlation == doctest::Approx (-1.0).epsilon (0.001));

        // Unrelated left and right: about 0.
        juce::Random random (42);
        juce::AudioBuffer<float> noise (2, (int) (rate * 2.0));

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < noise.getNumSamples(); ++i)
                noise.setSample (channel, i, random.nextFloat() - 0.5f);

        CHECK (std::abs (balanceOf (noise).correlation) < 0.05);

        // One channel is mono; silence is no problem.
        juce::AudioBuffer<float> one (1, (int) rate);
        one.copyFrom (0, 0, sine (440.0), 0, 0, one.getNumSamples());
        CHECK (balanceOf (one).correlation == doctest::Approx (1.0));

        juce::AudioBuffer<float> silence (2, (int) rate);
        silence.clear();
        const auto quiet = balanceOf (silence);
        CHECK (quiet.correlation == doctest::Approx (1.0));
        CHECK (quiet.lowDb == doctest::Approx (0.0));
        CHECK (quiet.highDb == doctest::Approx (0.0));
    }

    TEST_CASE ("a good mix passes; each clear fault is named, once")
    {
        CHECK (mix_check::findProblems (goodMix()).isEmpty());

        const auto onlyProblem = [] (const std::function<void (mix_check::Report&)>& spoil)
        {
            auto report = goodMix();
            spoil (report);
            const auto problems = mix_check::findProblems (report);
            CHECK (problems.size() == 1);
            return problems.joinIntoString ("\n");
        };

        CHECK (onlyProblem ([] (auto& r) { r.mix.peakDb = 0.0f; }).contains ("clips"));
        CHECK (onlyProblem ([] (auto& r) { r.balance.correlation = -0.3; }).contains ("work against each other"));
        CHECK (onlyProblem ([] (auto& r) { r.balance.correlation = 0.1; }).contains ("Very wide stereo"));
        CHECK (onlyProblem ([] (auto& r) { r.balance.lowDb = 14.0; }).contains ("A lot of bass"));
        CHECK (onlyProblem ([] (auto& r) { r.balance.lowDb = -15.0; }).contains ("Thin"));
        CHECK (onlyProblem ([] (auto& r) { r.balance.highDb = -3.0; }).contains ("Bright"));
        CHECK (onlyProblem ([] (auto& r) { r.balance.highDb = -45.0; }).contains ("Dull"));
        CHECK (onlyProblem ([] (auto& r) { r.tracks[2].level->lufs = -35.0; }) == "\"Pad\" is 19 LU quieter than \"Drums\": "
                                                                                  "it may get lost. Raise it, unless it's meant to be that quiet.");

        // Just inside the limits: nothing.
        auto edge = goodMix();
        edge.mix.peakDb = -0.2f;
        edge.balance = { 0.25, 9.0, -7.0 };
        edge.tracks[2].level->lufs = -30.0;
        CHECK (mix_check::findProblems (edge).isEmpty());

        // A silent track is never "lost"; at most three quiet ones are named.
        auto many = goodMix();

        for (int i = 0; i < 5; ++i)
            many.tracks.push_back ({ "Whisper " + juce::String (i + 1), Measurement { -40.0, -30.0f, 180.0 } });

        const auto problems = mix_check::findProblems (many);
        CHECK (problems.size() == 3);
        CHECK (problems[0].startsWith ("\"Whisper 1\""));
        CHECK_FALSE (problemsOf (many).contains ("Unused"));
    }

    TEST_CASE ("the report shows the numbers, the problems and the tracks")
    {
        const auto good = mix_check::describe (goodMix());
        CHECK (good.startsWith ("The mix: -14.0 LUFS, peaks at -1.0 dB. Stereo correlation 0.70. Bass +2.0 dB and top -20.0 dB"));
        CHECK (good.contains ("Nothing stands out"));
        CHECK (good.contains ("Tracks, loudest first:\nDrums: -16.0 LUFS\nBass: -18.0 LUFS\nPad: -25.0 LUFS\nUnused: silent\n"));
        CHECK (good.contains ("Check loudness for streaming"));

        auto clipping = goodMix();
        clipping.mix.peakDb = 0.0f;
        const auto bad = mix_check::describe (clipping);
        CHECK (bad.contains (juce::String::fromUTF8 ("\xe2\x80\xa2 It clips")));
        CHECK_FALSE (bad.contains ("Nothing stands out"));

        // A long list is cut short.
        auto big = goodMix();

        for (int i = 0; i < 20; ++i)
            big.tracks.push_back ({ "More", std::nullopt });

        CHECK (mix_check::describe (big).contains ("... and 12 more"));
    }

    TEST_CASE ("a song is checked as it is heard, track by track")
    {
        auto project = core::Project::createEmpty (test::session());

        const auto addPart = [&project] (te::AudioTrack* track, const juce::String& name, std::initializer_list<int> pitches)
            -> te::AudioTrack&
        {
            if (track == nullptr)
                track = project->addTrack (name);

            REQUIRE (track != nullptr);
            track->setName (name);
            REQUIRE (audio::instruments::useBuiltInSynth (*track).isEmpty());

            auto* clip = project->addMidiClip (*track, 0.0, 8.0, name);
            REQUIRE (clip != nullptr);

            for (int pitch : pitches)
                clip->getSequence().addNote (pitch, te::BeatPosition(), te::BeatDuration::fromBeats (8.0), 60, 0, nullptr);

            return *track;
        };

        addPart (project->getTracks().getFirst(), "Chords", { 60, 64, 67 });
        auto& whisper = addPart (nullptr, "Whisper", { 72 });
        whisper.getVolumePlugin()->setVolumeDb (-25.0f);
        addPart (nullptr, "Muted", { 48 }).setMute (true);
        REQUIRE (project->addTrack ("Empty") != nullptr);

        std::vector<juce::String> steps;
        juce::String error;
        const auto report = mix_check::check (project->getEdit(), error, [&steps] (const juce::String& step) { steps.push_back (step); });
        REQUIRE_MESSAGE (report.has_value(), error.toStdString());

        CHECK (report->mix.lufs < -10.0);
        CHECK (report->mix.lufs > -50.0);
        CHECK (report->mix.peakDb < 0.0f);
        CHECK (report->balance.correlation > 0.5);      // the built-in synth plays (mostly) in the middle
        CHECK (report->balance.lowDb < 0.0);            // a chord around middle C: little bass

        // The tracks that are heard and have clips, loudest first; the muted and the empty one aren't.
        REQUIRE (report->tracks.size() == 2);
        CHECK (report->tracks[0].name == "Chords");
        CHECK (report->tracks[1].name == "Whisper");
        REQUIRE (report->tracks[0].level.has_value());
        REQUIRE (report->tracks[1].level.has_value());
        CHECK (report->tracks[0].level->lufs - report->tracks[1].level->lufs > 20.0);
        CHECK (problemsOf (*report).contains ("\"Whisper\" is"));

        CHECK (steps == std::vector<juce::String> { "Mix Doctor: listening to the mix...", "Mix Doctor: listening to track 1 of 2...",
                                                    "Mix Doctor: listening to track 2 of 2..." });

        // Nothing to hear: says so.
        auto empty = core::Project::createEmpty (test::session());
        CHECK_FALSE (mix_check::check (empty->getEdit(), error).has_value());
        CHECK (error.startsWith ("Nothing to check"));
    }

    TEST_CASE ("Mix Doctor fixes the levels: lost tracks up, the master down until the peaks are at -1 dB, one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& edit = project->getEdit();

        const auto addPart = [&project] (te::AudioTrack* track, const juce::String& name, std::initializer_list<int> pitches,
                                         int velocity) -> te::AudioTrack&
        {
            if (track == nullptr)
                track = project->addTrack (name);

            REQUIRE (track != nullptr);
            track->setName (name);
            REQUIRE (audio::instruments::useBuiltInSynth (*track).isEmpty());
            auto* clip = project->addMidiClip (*track, 0.0, 8.0, name);
            REQUIRE (clip != nullptr);

            for (int pitch : pitches)
                clip->getSequence().addNote (pitch, te::BeatPosition(), te::BeatDuration::fromBeats (8.0), velocity, 0, nullptr);

            return *track;
        };

        // Loud chords that clip with the master at +6 dB; a pad far under them.
        addPart (project->getTracks().getFirst(), "Chords", { 48, 52, 55, 60, 64, 67 }, 127);
        auto& pad = addPart (nullptr, "Pad", { 72 }, 60);
        pad.getVolumePlugin()->setVolumeDb (-20.0f);
        edit.getMasterVolumePlugin()->setVolumeDb (6.0f);
        project->clearUndoHistory();

        juce::String error;
        const auto report = mix_check::check (edit, error);
        REQUIRE_MESSAGE (report.has_value(), error.toStdString());
        REQUIRE (report->mix.peakDb > -0.1f);
        REQUIRE (report->tracks.size() == 2);
        CHECK (report->tracks[1].name == "Pad");
        CHECK (report->tracks[1].track == pad.itemID);

        const auto fixes = mix_check::findFixes (*report, edit);
        CHECK (fixes.clips);
        REQUIRE (fixes.raise.size() == 1);
        CHECK (fixes.raise[0].first == pad.itemID);
        const auto gap = report->tracks[0].level->lufs - report->tracks[1].level->lufs;
        CHECK (fixes.raise[0].second == doctest::Approx (std::min (gap - mix_check::raisedLu, 6.0 + 20.0)).epsilon (0.01));
        CHECK (fixes.descriptions.joinIntoString ("; ").contains ("raise \"Pad\" by"));
        CHECK (fixes.descriptions.joinIntoString ("; ").contains ("bring the master down"));

        std::vector<juce::String> steps;
        project->beginGesture ("Mix Doctor fixes");
        const auto done = mix_check::applyFixes (edit, fixes, error, [&steps] (const juce::String& step) { steps.push_back (step); });
        CHECK (error.isEmpty());
        REQUIRE (done.size() == 2);
        CHECK (done[0].startsWith ("master -"));
        CHECK (done[1].startsWith ("\"Pad\" +"));
        CHECK (! steps.empty());

        CHECK (pad.getVolumePlugin()->getVolumeDb() == doctest::Approx (-20.0f + fixes.raise[0].second).epsilon (0.01));
        CHECK (edit.getMasterVolumePlugin()->getVolumeDb() < 6.0f);

        // The peaks now under the ceiling (the synth's phases change a little from render to render).
        const auto after = audio::loudness::measureSong (edit, error);
        REQUIRE (after.has_value());
        CHECK (after->peakDb < -0.3f);
        CHECK (after->peakDb > -2.5f);

        project->undo();
        CHECK (pad.getVolumePlugin()->getVolumeDb() == doctest::Approx (-20.0f));
        CHECK (edit.getMasterVolumePlugin()->getVolumeDb() == doctest::Approx (6.0f));
    }

    TEST_CASE ("nothing to fix, nothing changes; a track as loud as its fader goes isn't offered")
    {
        auto project = core::Project::createEmpty (test::session());
        auto* quiet = project->getTracks().getFirst();
        quiet->getVolumePlugin()->setVolumeDb (6.0f);

        auto report = goodMix();
        CHECK (mix_check::findFixes (report, project->getEdit()).isEmpty());

        // Lost, but its fader is already at the top.
        report.tracks[2].level = Measurement { -40.0, -20.0f, 180.0 };
        report.tracks[2].track = quiet->itemID;
        CHECK (problemsOf (report).contains ("\"Pad\" is 24 LU quieter"));
        CHECK (mix_check::findFixes (report, project->getEdit()).isEmpty());

        juce::String error;
        CHECK (mix_check::applyFixes (project->getEdit(), {}, error).isEmpty());
        CHECK (error.isEmpty());
    }
}
