#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/FilePreview.h"
#include "audio/StretchedSource.h"

using namespace harmonia;
using audio::StretchedSource;

namespace
{
    constexpr double rate = 44100.0;

    std::unique_ptr<StretchedSource> open (const juce::File& file, double speed)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto* reader = formats.createReaderFor (file);
        REQUIRE (reader != nullptr);

        const auto numChannels = (int) reader->numChannels;
        const auto sampleRate = reader->sampleRate;
        return std::make_unique<StretchedSource> (std::make_unique<juce::AudioFormatReaderSource> (reader, true),
                                                  sampleRate, numChannels, speed);
    }

    /** What the source gives from where it is, asked for `pieceSize` at a time. */
    juce::AudioBuffer<float> read (StretchedSource& source, int numSamples, int pieceSize = 512)
    {
        juce::AudioBuffer<float> sound (2, numSamples);
        sound.clear();

        for (int done = 0; done < numSamples;)
        {
            const auto count = juce::jmin (pieceSize, numSamples - done);
            source.getNextAudioBlock (juce::AudioSourceChannelInfo (&sound, done, count));
            done += count;
        }

        return sound;
    }

    /** Where the clicks of a sound start, in seconds: louder than a third of its loudest moment
        after a tenth of a second that wasn't. */
    std::vector<double> findClicks (const juce::AudioBuffer<float>& sound)
    {
        const auto threshold = sound.getMagnitude (0, 0, sound.getNumSamples()) / 3.0f;
        std::vector<double> clicks;
        auto lastLoud = -(int) rate;

        for (int i = 0; i < sound.getNumSamples(); ++i)
        {
            if (threshold <= 0.0f || std::abs (sound.getSample (0, i)) <= threshold)
                continue;

            if (i - lastLoud > (int) (0.1 * rate))
                clicks.push_back (i / rate);

            lastLoud = i;
        }

        return clicks;
    }

    /** How many times a second the sound goes through zero between two moments (twice its pitch). */
    double crossingsPerSecond (const juce::AudioBuffer<float>& sound, double fromSeconds, double toSeconds)
    {
        const auto from = (int) (fromSeconds * rate), to = juce::jmin ((int) (toSeconds * rate), sound.getNumSamples() - 1);
        int crossings = 0;

        for (int i = from; i < to; ++i)
            if ((sound.getSample (0, i) < 0.0f) != (sound.getSample (0, i + 1) < 0.0f))
                ++crossings;

        return crossings / (toSeconds - fromSeconds);
    }

    void checkClicks (const std::vector<double>& found, const std::vector<double>& expected)
    {
        REQUIRE (found.size() == expected.size());

        for (size_t i = 0; i < expected.size(); ++i)
        {
            CAPTURE (found[i]);
            CAPTURE (expected[i]);
            CHECK (std::abs (found[i] - expected[i]) < 0.025);
        }
    }
}

TEST_SUITE ("audio: a sound heard at another tempo")
{
    TEST_CASE ("played faster or slower, its hits come as much sooner or later, and its pitch stays")
    {
        test::TempFolder temp;
        const auto clicks = test::writeClicks (temp.getFile ("clicks.wav"), { 0.5, 1.0, 1.5, 2.0 }, 0.5);
        const auto tone = test::writeTone (temp.getFile ("tone.wav"), 2.0);

        {
            // A quarter faster: what took 2.5 seconds takes 2.
            auto faster = open (clicks, 1.25);
            CHECK (faster->getSpeed() == doctest::Approx (1.25));
            CHECK (faster->getTotalLength() == (juce::int64) std::ceil (2.5 * rate / 1.25));
            CHECK_FALSE (faster->isLooping());

            const auto sound = read (*faster, (int) faster->getTotalLength());
            checkClicks (findClicks (sound), { 0.4, 0.8, 1.2, 1.6 });
            CHECK (faster->getNextReadPosition() == faster->getTotalLength());

            // A file of one channel is heard on both.
            CHECK (sound.getMagnitude (1, 0, sound.getNumSamples()) == doctest::Approx (sound.getMagnitude (0, 0, sound.getNumSamples())));
        }

        {
            // A fifth slower.
            auto slower = open (clicks, 0.8);
            CHECK (slower->getTotalLength() == (juce::int64) std::ceil (2.5 * rate / 0.8));
            checkClicks (findClicks (read (*slower, (int) slower->getTotalLength())), { 0.625, 1.25, 1.875, 2.5 });
        }

        // A tone of 220 Hz is one of 220 Hz at either speed, and as loud.
        for (const auto speed : { 0.8, 1.25 })
        {
            CAPTURE (speed);
            auto source = open (tone, speed);
            const auto sound = read (*source, (int) source->getTotalLength());
            const auto middle = 1.0 / speed;      // a second into the tone, as it is heard
            CHECK (crossingsPerSecond (sound, middle - 0.4, middle + 0.4) == doctest::Approx (440.0).epsilon (0.03));
            CHECK (sound.getRMSLevel (0, (int) ((middle - 0.4) * rate), (int) (0.8 * rate)) == doctest::Approx (0.5 / std::sqrt (2.0)).epsilon (0.15));
        }

        // Speeds beyond what it plays at are brought within.
        CHECK (open (clicks, 100.0)->getSpeed() == doctest::Approx (StretchedSource::maxSpeed));
        CHECK (open (clicks, 0.001)->getSpeed() == doctest::Approx (StretchedSource::minSpeed));
    }

    TEST_CASE ("asked for in pieces of any size it gives the same sound; from another place, what is heard there")
    {
        test::TempFolder temp;
        const auto clicks = test::writeClicks (temp.getFile ("clicks.wav"), { 0.5, 1.0, 1.5, 2.0 }, 0.5);

        auto one = open (clicks, 1.25), other = open (clicks, 1.25);
        const auto length = (int) one->getTotalLength();
        const auto whole = read (*one, length, 512);
        const auto inPieces = read (*other, length, 1000);

        auto difference = 0.0f;

        for (int i = 0; i < length; ++i)
            difference = juce::jmax (difference, std::abs (whole.getSample (0, i) - inPieces.getSample (0, i)));

        CHECK (difference < 1.0e-6f);

        // From 0.7 seconds in (of what is heard): the hits at 0.8 and 1.2 come 0.1 and 0.5 later.
        other->setNextReadPosition ((juce::int64) (0.7 * rate));
        CHECK (other->getNextReadPosition() == (juce::int64) (0.7 * rate));
        checkClicks (findClicks (read (*other, (int) (0.75 * rate), 37 * 16)), { 0.1, 0.5 });
        CHECK (other->getNextReadPosition() == (juce::int64) (0.7 * rate) + (int) (0.75 * rate));

        // And from the start again.
        other->setNextReadPosition (0);
        checkClicks (findClicks (read (*other, (int) (0.75 * rate))), { 0.4 });
    }

    TEST_CASE ("the preview plays a loop at the song's tempo: as long as its beats take there; anything else as it is")
    {
        test::TempFolder temp;
        audio::FilePreview preview (test::session());
        const auto loop = test::writeLoop (temp.getFile ("Drums 95 BPM.wav"), 95.0, 16);
        const auto hit = test::writeTone (temp.getFile ("Kick.wav"), 0.5);
        const auto ownSeconds = 16.0 * 60.0 / 95.0;

        // No tempo asked for: as the file is.
        REQUIRE (preview.play (loop));
        CHECK_FALSE (preview.getTempo().has_value());
        CHECK (preview.getLengthSeconds() == doctest::Approx (ownSeconds).epsilon (0.001));

        // At 120 its sixteen beats take eight seconds.
        REQUIRE (preview.play (loop, 120.0));
        CHECK (preview.isPlaying());
        REQUIRE (preview.getTempo().has_value());
        CHECK (preview.getTempo()->own == doctest::Approx (95.0).epsilon (0.002));
        CHECK (preview.getTempo()->heard == doctest::Approx (120.0));
        CHECK (preview.getLengthSeconds() == doctest::Approx (8.0).epsilon (0.003));

        // Slower than it is, too.
        REQUIRE (preview.play (loop, 60.0));
        CHECK (preview.getLengthSeconds() == doctest::Approx (16.0).epsilon (0.003));

        // At its own tempo there is nothing to stretch.
        REQUIRE (preview.play (loop, 95.0));
        REQUIRE (preview.getTempo().has_value());
        CHECK (preview.getLengthSeconds() == doctest::Approx (ownSeconds).epsilon (0.003));

        // More than four times slower it wouldn't be the loop any more: as it is.
        REQUIRE (preview.play (loop, 20.0));
        CHECK_FALSE (preview.getTempo().has_value());
        CHECK (preview.getLengthSeconds() == doctest::Approx (ownSeconds).epsilon (0.001));

        // A drum hit plays as it is whatever the song's tempo.
        REQUIRE (preview.play (hit, 140.0));
        CHECK_FALSE (preview.getTempo().has_value());
        CHECK (preview.getLengthSeconds() == doctest::Approx (0.5).epsilon (0.01));

        REQUIRE (preview.play (loop, 120.0));
        preview.stop();
        CHECK_FALSE (preview.isPlaying());
        CHECK_FALSE (preview.getTempo().has_value());
    }
}
