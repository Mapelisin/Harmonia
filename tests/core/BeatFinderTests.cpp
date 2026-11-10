#include <doctest.h>

#include <cmath>
#include <random>
#include <vector>

#include "core/BeatFinder.h"

using namespace harmonia::core;

namespace
{
    constexpr double twoPi = 6.283185307179586;

    /** A sound being put together: silence to begin with. */
    struct Sound
    {
        Sound (double seconds, double sampleRate = 44100.0)
            : rate (sampleRate), samples ((size_t) std::llround (seconds * sampleRate), 0.0f) {}

        double getSeconds() const       { return samples.size() / rate; }

        /** A short click (a kilohertz, gone in a few milliseconds). */
        void click (double at, float level = 0.9f)
        {
            add (at, 0.012, [level, this] (double t) { return level * std::sin (twoPi * 1000.0 * t) * std::exp (-t / 0.003); });
        }

        /** A kick: a low thump falling in pitch. */
        void kick (double at, float level = 0.9f)
        {
            add (at, 0.22, [level] (double t) { return level * std::sin (twoPi * (50.0 * t + 40.0 * (1.0 - std::exp (-t / 0.03)) * 0.03)) * std::exp (-t / 0.07); });
        }

        /** A snare: a burst of noise with a body. */
        void snare (double at, float level = 0.6f)
        {
            add (at, 0.16, [level, this] (double t) { return level * (0.7 * noise() + 0.3 * std::sin (twoPi * 190.0 * t)) * std::exp (-t / 0.045); });
        }

        /** A closed hat: a very short hiss. */
        void hat (double at, float level = 0.25f)
        {
            auto last = 0.0;
            add (at, 0.04, [level, this, last] (double t) mutable
            {
                const auto n = noise();
                const auto high = n - last;        // the hiss without its lows
                last = n;
                return level * high * std::exp (-t / 0.008);
            });
        }

        /** A plucked note: a tone that comes in over a few milliseconds and rings out. */
        void pluck (double at, double hz, float level = 0.5f)
        {
            add (at, 0.35, [level, hz] (double t)
            {
                return level * (1.0 - std::exp (-t / 0.004)) * std::exp (-t / 0.12)
                         * (std::sin (twoPi * hz * t) + 0.4 * std::sin (twoPi * 2.0 * hz * t));
            });
        }

        /** A hiss under everything, as a recording has. */
        void addNoiseFloor (float level)
        {
            for (auto& s : samples)
                s += level * (float) noise();
        }

        beat_finder::Beat analyse (double nearBpm) const
        {
            const auto beat = beat_finder::analyse (samples.data(), (int) samples.size(), rate, nearBpm);
            REQUIRE (beat.has_value());
            return *beat;
        }

        bool hasBeat (double nearBpm = 120.0) const
        {
            return beat_finder::analyse (samples.data(), (int) samples.size(), rate, nearBpm).has_value();
        }

        std::vector<beat_finder::Hit> hits() const
        {
            return beat_finder::findHits (samples.data(), (int) samples.size(), rate);
        }

        double rate;
        std::vector<float> samples;

    private:
        std::mt19937 random { 12345 };

        double noise()      { return std::uniform_real_distribution<double> (-1.0, 1.0) (random); }

        template <typename Fn>
        void add (double at, double length, Fn&& fn)
        {
            const auto start = (long long) std::llround (at * rate);

            for (long long i = 0; i < (long long) (length * rate); ++i)
                if (start + i >= 0 && start + i < (long long) samples.size())
                    samples[(size_t) (start + i)] += (float) fn (i / rate);
        }
    };

    /** Kick on one and three, snare on two and four, hats on the eighths; `lead` seconds of
        silence before the first beat, `tail` after the last bar. */
    Sound drums (double bpm, int bars, double lead = 0.0, double tail = 0.0, double sampleRate = 44100.0)
    {
        const auto beat = 60.0 / bpm;
        Sound sound (lead + bars * 4 * beat + tail, sampleRate);

        for (int b = 0; b < bars * 4; ++b)
        {
            const auto at = lead + b * beat;

            if (b % 2 == 0)     sound.kick (at);
            else                sound.snare (at);

            sound.hat (at);
            sound.hat (at + beat / 2.0);
        }

        return sound;
    }

    /** The same drums played by hand: each beat where `beats` has it (seconds), the eighths half
        way between; the snares `lateSnare` seconds behind their beats. Nothing is played from
        the beat `restFrom` up to the beat `restTo`. */
    Sound playedDrums (const std::vector<double>& beats, double lateSnare = 0.0, size_t restFrom = 0, size_t restTo = 0)
    {
        Sound sound (beats.back() + 0.5);

        for (size_t b = 0; b + 1 < beats.size(); ++b)
        {
            if (b >= restFrom && b < restTo)
                continue;

            if (b % 2 == 0)     sound.kick (beats[b]);
            else                sound.snare (beats[b] + lateSnare);

            sound.hat (beats[b]);
            sound.hat ((beats[b] + beats[b + 1]) / 2.0);
        }

        return sound;
    }

    /** Where the beats of somebody playing are: `count` of them from `lead` seconds on, the tempo
        going from `fromBpm` to `toBpm` on the way and back `waves` times, each beat up to
        `looseness` seconds early or late. */
    std::vector<double> playedBeats (int count, double lead, double fromBpm, double toBpm, double waves = 0.5, double looseness = 0.0)
    {
        std::mt19937 random (777);
        std::uniform_real_distribution<double> loose (-1.0, 1.0);
        std::vector<double> beats;
        auto at = lead;

        for (int i = 0; i < count; ++i)
        {
            beats.push_back (at + looseness * loose (random));
            const auto along = 0.5 - 0.5 * std::cos (twoPi * waves * i / (count - 1.0));
            at += 60.0 / (fromBpm + (toBpm - fromBpm) * along);
        }

        return beats;
    }
}

TEST_SUITE ("core: the beat of a sound")
{
    TEST_CASE ("the hits of a sound are found where they start, to the millisecond")
    {
        Sound sound (3.0);

        for (const auto at : { 0.0, 0.5, 1.0, 1.25, 2.1 })
            sound.click (at);

        const auto hits = sound.hits();
        REQUIRE (hits.size() == 5);
        CHECK (hits[0].seconds == doctest::Approx (0.0).epsilon (0.001));
        CHECK (hits[1].seconds == doctest::Approx (0.5).epsilon (0.002));
        CHECK (hits[3].seconds == doctest::Approx (1.25).epsilon (0.001));
        CHECK (hits[4].seconds == doctest::Approx (2.1).epsilon (0.001));

        for (const auto& hit : hits)
            CHECK (hit.strength > 0.5f);

        // A quiet hit among loud ones is still a hit, a weaker one; under the hiss of a recording too.
        Sound mixed (3.0);
        mixed.kick (0.5);
        mixed.hat (1.0, 0.1f);
        mixed.kick (1.5);
        mixed.addNoiseFloor (0.003f);
        const auto found = mixed.hits();
        REQUIRE (found.size() == 3);
        CHECK (found[1].seconds == doctest::Approx (1.0).epsilon (0.003));
        CHECK (found[1].strength < found[0].strength);

        // Nothing in silence, nothing in a held tone.
        CHECK (Sound (2.0).hits().empty());
    }

    TEST_CASE ("a loop cut to length: the tempo as it is (72.3), from the first sample")
    {
        const auto bpm = 72.3;
        Sound loop (16 * 60.0 / bpm);

        for (int beat = 0; beat < 16; ++beat)
            loop.click (beat * 60.0 / bpm);

        auto beat = loop.analyse (72.0);
        CHECK (beat.bpm == doctest::Approx (72.3).epsilon (0.0002));
        CHECK (beat.wholeLoop);
        CHECK (beat.firstBeatSeconds == 0.0);
        CHECK (beat.certainty > 0.95);
        CHECK (beat.offGridMs < 1.0);

        // In a faster song the same hits are beats twice as fast: the nearer tempo is taken.
        beat = loop.analyse (150.0);
        CHECK (beat.bpm == doctest::Approx (144.6).epsilon (0.0002));
        CHECK (beat.wholeLoop);

        // And half as fast in a very slow one? Not below what a beat can be.
        beat = loop.analyse (50.0);
        CHECK (beat.bpm == doctest::Approx (72.3).epsilon (0.0002));
    }

    TEST_CASE ("drums with silence before and an odd tail: the tempo to a few hundredths, the first beat to a few milliseconds")
    {
        for (const auto bpm : { 93.7, 120.0, 128.0, 140.25, 174.0, 67.5 })
        {
            CAPTURE (bpm);
            auto sound = drums (bpm, 4, 0.35, 0.21);
            sound.addNoiseFloor (0.002f);
            const auto beat = sound.analyse (bpm * 1.07);       // the song is a little faster

            CHECK (beat.bpm == doctest::Approx (bpm).epsilon (0.0005));
            CHECK (beat.firstBeatSeconds == doctest::Approx (0.35).epsilon (0.01));
            CHECK (std::abs (beat.firstBeatSeconds - 0.35) < 0.004);
            CHECK_FALSE (beat.wholeLoop);
            CHECK (beat.certainty > 0.8);
            CHECK (beat.offGridMs < 3.0);
        }

        // Other sample rates, a longer piece.
        for (const auto rate : { 48000.0, 22050.0, 96000.0 })
        {
            CAPTURE (rate);
            const auto beat = drums (101.3, 8, 0.12, 0.4, rate).analyse (100.0);
            CHECK (beat.bpm == doctest::Approx (101.3).epsilon (0.0005));
            CHECK (std::abs (beat.firstBeatSeconds - 0.12) < 0.004);
        }

        // Cut to whole bars, drums are a loop too.
        const auto loop = drums (87.4, 2).analyse (90.0);
        CHECK (loop.wholeLoop);
        CHECK (loop.bpm == doctest::Approx (87.4).epsilon (0.0002));
        CHECK (loop.firstBeatSeconds == 0.0);
    }

    TEST_CASE ("other rhythms: syncopated kicks under sixteenth hats, a bass line with plucked chords, hats alone")
    {
        // 126 BPM: kicks on 1, the "and" of 2 and 4 (3 + 3 + 2 sixteenth-pairs), hats on every sixteenth, louder on the beat.
        {
            const auto bpm = 126.0, beat = 60.0 / bpm;
            Sound sound (0.18 + 4 * 4 * beat + 0.3);

            for (int bar = 0; bar < 4; ++bar)
            {
                const auto start = 0.18 + bar * 4 * beat;

                for (const auto at : { 0.0, 1.5, 3.0 })
                    sound.kick (start + at * beat);

                sound.snare (start + 1.0 * beat);
                sound.snare (start + 3.0 * beat, 0.5f);

                for (int sixteenth = 0; sixteenth < 16; ++sixteenth)
                    sound.hat (start + sixteenth * beat / 4.0, sixteenth % 4 == 0 ? 0.3f : 0.15f);
            }

            sound.addNoiseFloor (0.002f);
            const auto found = sound.analyse (120.0);
            CHECK (found.bpm == doctest::Approx (126.0).epsilon (0.0005));
            CHECK (std::abs (found.firstBeatSeconds - 0.18) < 0.004);
            CHECK (found.certainty > 0.8);
        }

        // 88 BPM: a bass note on every beat, a plucked chord on the off-beats: nothing sharp in it.
        {
            const auto bpm = 88.0, beat = 60.0 / bpm;
            Sound sound (0.4 + 8 * beat + 0.5);

            for (int b = 0; b < 8; ++b)
            {
                sound.pluck (0.4 + b * beat, b % 4 == 0 ? 55.0 : 82.4, 0.6f);
                sound.pluck (0.4 + (b + 0.5) * beat, 330.0, 0.3f);
                sound.pluck (0.4 + (b + 0.5) * beat, 415.3, 0.25f);
            }

            const auto found = sound.analyse (90.0);
            CHECK (found.bpm == doctest::Approx (88.0).epsilon (0.001));
            CHECK (std::abs (found.firstBeatSeconds - 0.4) < 0.008);     // a soft attack: a few milliseconds more
        }

        // 100 BPM: hats alone, sixteenths, the beat only in their accents; cut to two bars.
        {
            const auto bpm = 100.0, beat = 60.0 / bpm;
            Sound sound (8 * beat);

            for (int sixteenth = 0; sixteenth < 32; ++sixteenth)
                sound.hat (sixteenth * beat / 4.0, sixteenth % 4 == 0 ? 0.5f : (sixteenth % 2 == 0 ? 0.25f : 0.15f));

            const auto found = sound.analyse (100.0);
            CHECK (found.bpm == doctest::Approx (100.0).epsilon (0.0005));
            CHECK (found.wholeLoop);
        }
    }

    TEST_CASE ("drums that speed up: no one tempo fits, each beat is found where it is")
    {
        // From 92 to 104 BPM over twelve bars.
        const auto beats = playedBeats (49, 0.3, 92.0, 104.0);
        auto sound = playedDrums (beats);
        sound.addNoiseFloor (0.002f);
        const auto beat = sound.analyse (100.0);

        REQUIRE (beat.drifts());
        CHECK_FALSE (beat.wholeLoop);
        CHECK (beat.certainty > 0.8);
        CHECK (std::abs (beat.firstBeatSeconds - 0.3) < 0.004);
        CHECK (beat.bpm == doctest::Approx (60.0 * 47.0 / (beats[47] - beats[0])).epsilon (0.01));

        // All the beats that have a hit (the last of `beats` is where the sound ends).
        REQUIRE (beat.beatSeconds.size() >= 48);

        for (size_t i = 0; i < 48; ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (beat.beatSeconds[i] - beats[i]) < 0.003);
        }
    }

    TEST_CASE ("a tempo that comes and goes, each beat a little loose: the beats are where the player put them")
    {
        // Between 118 and 126 BPM, there and back twice, every beat up to 8 ms off.
        const auto beats = playedBeats (65, 0.5, 118.0, 126.0, 2.0, 0.008);
        const auto beat = playedDrums (beats).analyse (120.0);

        REQUIRE (beat.drifts());
        REQUIRE (beat.beatSeconds.size() >= 64);

        for (size_t i = 0; i < 64; ++i)
        {
            CAPTURE (i);
            CHECK (std::abs (beat.beatSeconds[i] - beats[i]) < 0.003);
        }

        // In a song twice as fast it is counted in twice the beats: each of them and one between.
        const auto twice = playedDrums (beats).analyse (240.0);
        REQUIRE (twice.drifts());
        REQUIRE (twice.beatSeconds.size() >= 127);
        CHECK (std::abs (twice.beatSeconds[20] - beats[10]) < 0.003);
        CHECK (std::abs (twice.beatSeconds[21] - (beats[10] + beats[11]) / 2.0) < 0.004);
        CHECK (twice.bpm == doctest::Approx (2.0 * beat.bpm).epsilon (0.01));
    }

    TEST_CASE ("a break of three bars, counted in the head a little fast: the beats after it are the right ones")
    {
        // Four bars at 118 BPM, three bars of nothing counted at 121, four bars at 118 again.
        std::vector<double> beats;
        auto at = 0.4;

        for (int i = 0; i < 45; ++i)
        {
            beats.push_back (at);
            at += 60.0 / (i >= 16 && i < 28 ? 121.0 : 118.0);
        }

        const auto beat = playedDrums (beats, 0.0, 16, 28).analyse (120.0);
        REQUIRE (beat.drifts());
        REQUIRE (beat.beatSeconds.size() >= 44);

        // By the tempo before it, the break is some 90 ms longer than it was: most of a sixteenth.
        // The drums come back in on a beat, and that settles which.
        for (size_t i = 0; i < 44; ++i)
        {
            if (i >= 16 && i < 28)
                continue;

            CAPTURE (i);
            CHECK (std::abs (beat.beatSeconds[i] - beats[i]) < 0.003);
        }

        // The beats nobody played are spread evenly over the break.
        CHECK (std::abs (beat.beatSeconds[22] - beats[22]) < 0.02);
    }

    TEST_CASE ("feel is not drift: a snare that is always late leaves the tempo steady, and a machine's drums too")
    {
        // 100 BPM by the clock, every snare 18 ms behind.
        const auto beats = playedBeats (33, 0.2, 100.0, 100.0);
        const auto laidBack = playedDrums (beats, 0.018).analyse (100.0);
        CHECK_FALSE (laidBack.drifts());
        CHECK (laidBack.bpm == doctest::Approx (100.0).epsilon (0.001));
        CHECK (std::abs (laidBack.firstBeatSeconds - 0.2) < 0.004);

        CHECK_FALSE (drums (93.7, 8, 0.35, 0.21).analyse (100.0).drifts());
        CHECK_FALSE (drums (140.25, 16, 0.1, 0.3).analyse (128.0).drifts());
    }

    TEST_CASE ("the tempo nearest the song's is taken: half or double of what another song would hear")
    {
        auto sound = drums (80.0, 4, 0.2, 0.3);
        CHECK (sound.analyse (85.0).bpm == doctest::Approx (80.0).epsilon (0.001));
        CHECK (sound.analyse (150.0).bpm == doctest::Approx (160.0).epsilon (0.001));
        CHECK (sound.analyse (170.0).bpm == doctest::Approx (160.0).epsilon (0.001));

        // The first beat is the same hit either way.
        CHECK (std::abs (sound.analyse (150.0).firstBeatSeconds - 0.2) < 0.004);
    }

    TEST_CASE ("no beat is found where there is none: a held tone, one hit, hits at random, silence, a moment of sound")
    {
        Sound tone (4.0);

        for (size_t i = 0; i < tone.samples.size(); ++i)
            tone.samples[i] = 0.5f * (float) std::sin (twoPi * 220.0 * i / tone.rate);

        CHECK_FALSE (tone.hasBeat());

        Sound one (2.0);
        one.kick (0.1);
        CHECK_FALSE (one.hasBeat());

        Sound two (2.0);
        two.kick (0.1);
        two.snare (0.9);
        CHECK_FALSE (two.hasBeat());

        std::mt19937 random (7);
        Sound scattered (12.0);

        for (int i = 0; i < 40; ++i)
            scattered.click (std::uniform_real_distribution<double> (0.0, 11.9) (random), 0.5f + 0.4f * (float) (i % 3) / 2.0f);

        CHECK_FALSE (scattered.hasBeat());

        CHECK_FALSE (Sound (3.0).hasBeat());
        CHECK_FALSE (Sound (0.01).hasBeat());
        CHECK_FALSE (beat_finder::analyse (nullptr, 0, 44100.0, 120.0).has_value());
    }
}
