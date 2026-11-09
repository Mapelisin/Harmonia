#include <doctest.h>

#include "TestAudioRig.h"
#include "audio/Transport.h"
#include "core/AudioClips.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Project.h"

using namespace harmonia;
namespace clips = core::clips;
namespace audio_clips = core::audio_clips;

namespace
{
    /** A file of stretches of a 440 Hz tone (level > 0) and silence (level 0), each `seconds` long. */
    juce::File writeStretches (const juce::File& file, std::initializer_list<float> levels, double seconds)
    {
        constexpr double sampleRate = 44100.0;
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (sampleRate)
                                                                                     .withNumChannels (1)
                                                                                     .withBitsPerSample (16));
        REQUIRE (writer != nullptr);

        const auto each = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (1, each * (int) levels.size());
        int i = 0;

        for (const auto level : levels)
            for (int n = 0; n < each; ++n, ++i)
                buffer.setSample (0, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sampleRate));

        REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
        writer.reset();
        return file;
    }

    /** The song played from its start: the level heard in each stretch of `seconds`. */
    std::vector<float> playStretches (test::AudioRig& rig, int count, double seconds)
    {
        auto& edit = rig.project->getEdit();
        edit.dispatchPendingUpdatesSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        audio::Transport transport (edit);

        // A stretched clip's reader gets ready in the background once the song first plays it:
        // a moment of real time (the test plays faster than that).
        transport.setPositionSeconds (0.0);
        transport.play();
        rig.process (0.1, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        transport.stop();

        transport.setPositionSeconds (0.0);
        transport.play();
        std::vector<float> levels;

        for (int i = 0; i < count; ++i)
        {
            // The middle of the stretch: the edges may have a click's worth of the neighbour.
            rig.process (seconds * 0.2, 0.0f);
            levels.push_back (rig.process (seconds * 0.6, 0.0f));
            rig.process (seconds * 0.2, 0.0f);
        }

        transport.stop();
        return levels;
    }
}

TEST_SUITE ("audio: looping clips and crossfades")
{
    TEST_CASE ("a looping audio clip repeats the part it plays for as long as it is; off, it plays once")
    {
        test::AudioRig rig;
        auto& track = rig.addAudioTrack ("Loop");
        rig.project->setTempoBpm (120.0);       // a beat is half a second

        // Half a second of silence, of tone, then a second of silence: the clip plays only the tone.
        const auto file = writeStretches (rig.temp.getFile ("Parts.wav"), { 0.0f, 0.5f, 0.0f, 0.0f }, 0.5);
        juce::String error;
        auto* clip = dynamic_cast<te::AudioClipBase*> (rig.project->addAudioClip (track, file, 0.0, error));
        REQUIRE_MESSAGE (clip != nullptr, error.toStdString());
        clips::setStart (*clip, 1.0, 0.25);     // hides the silence at the start
        clips::setEnd (*clip, 2.0, 0.25);       // and after the tone
        clips::move (*clip, 0.0, track);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (1.0));
        CHECK_FALSE (audio_clips::isLooping (*clip));
        CHECK (audio_clips::getLoopLengthBeats (*clip) == doctest::Approx (0.0));
        rig.project->clearUndoHistory();

        // Not looping, it can't be longer than what's left of its file.
        clips::setEnd (*clip, 8.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (3.0).epsilon (0.01));
        clips::setEnd (*clip, 1.0, 0.25);

        rig.project->beginGesture ("Loop");
        audio_clips::setLooping (*clip, true);
        CHECK (audio_clips::isLooping (*clip));
        CHECK (audio_clips::getLoopLengthBeats (*clip) == doctest::Approx (1.0).epsilon (0.01));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (1.0));     // as long as it was

        // Made four times as long: the tone four times over, no silence between.
        clips::setEnd (*clip, 4.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (4.0));

        auto heard = playStretches (rig, 5, 0.5);
        for (int i = 0; i < 4; ++i)
            CHECK_MESSAGE (heard[(size_t) i] > 0.2f, "time round " << i);
        CHECK (heard[4] < 0.01f);       // after the clip

        // Off: once, from where the loop started; the file's silence after it, then nothing.
        rig.project->beginGesture ("No loop");
        audio_clips::setLooping (*clip, false);
        CHECK_FALSE (audio_clips::isLooping (*clip));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (1.0).epsilon (0.01));
        heard = playStretches (rig, 2, 0.5);
        CHECK (heard[0] > 0.2f);
        CHECK (heard[1] < 0.01f);

        rig.project->undo();
        CHECK (audio_clips::isLooping (*clip));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (4.0));
        rig.project->undo();
        CHECK_FALSE (audio_clips::isLooping (*clip));
    }

    TEST_CASE ("a loop that follows the tempo repeats in beats, and still does at another tempo")
    {
        test::AudioRig rig;
        auto& track = rig.addAudioTrack ("Loop");
        rig.project->setTempoBpm (120.0);

        // A bar at 120 BPM: a beat of tone, three of silence.
        const auto file = writeStretches (rig.temp.getFile ("Bar.wav"), { 0.5f, 0.0f, 0.0f, 0.0f }, 0.5);
        juce::String error;
        auto* clip = dynamic_cast<te::WaveAudioClip*> (rig.project->addAudioClip (track, file, 0.0, error));
        REQUIRE_MESSAGE (clip != nullptr, error.toStdString());
        core::audio_tempo::setFollowsTempo (*clip, 120.0);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (4.0).epsilon (0.01));

        audio_clips::setLooping (*clip, true);
        CHECK (audio_clips::isLooping (*clip));
        CHECK (audio_clips::getLoopLengthBeats (*clip) == doctest::Approx (4.0).epsilon (0.01));
        clips::setEnd (*clip, 12.0, 0.25);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (12.0));

        // The tone on the first beat of each of the three bars.
        auto heard = playStretches (rig, 12, 0.5);
        for (int beat = 0; beat < 12; ++beat)
            CHECK_MESSAGE ((heard[(size_t) beat] > 0.1f) == (beat % 4 == 0), "beat " << beat);

        // Slower: the same beats, each longer.
        rig.project->setTempoBpm (60.0);
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (12.0).epsilon (0.01));
        heard = playStretches (rig, 6, 1.0);
        for (int beat = 0; beat < 6; ++beat)
            CHECK_MESSAGE ((heard[(size_t) beat] > 0.1f) == (beat % 4 == 0), "beat " << beat << " at 60 BPM");
    }

    TEST_CASE ("where two audio clips overlap, one fades into the other; apart again, their own fades are back")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        auto& track = rig.addAudioTrack ("Vocals");
        rig.project->setTempoBpm (120.0);

        const auto file = test::writeTone (rig.temp.getFile ("Tone.wav"), 4.0, 0.25f);
        juce::String error;
        auto* first = dynamic_cast<te::AudioClipBase*> (rig.project->addAudioClip (track, file, 0.0, error));
        auto* second = dynamic_cast<te::AudioClipBase*> (rig.project->addAudioClip (track, file, 16.0, error));
        REQUIRE (first != nullptr);
        REQUIRE (second != nullptr);
        CHECK (audio_clips::hasCrossfades (*first));        // every clip, from the start
        CHECK (audio_clips::hasCrossfades (*second));

        clips::setFadeOutBeats (*first, 0.5);
        const auto dispatch = [&]
        {
            edit.dispatchPendingUpdatesSynchronously();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        };
        dispatch();
        CHECK (clips::getFadeOutBeats (*first) == doctest::Approx (0.5).epsilon (0.01));
        CHECK (clips::getFadeInBeats (*second) == doctest::Approx (0.0).epsilon (0.01));

        // The second moved over the last two beats of the first.
        clips::move (*second, 6.0, track);
        dispatch();
        CHECK (clips::getFadeOutBeats (*first) == doctest::Approx (2.0).epsilon (0.02));
        CHECK (clips::getFadeInBeats (*second) == doctest::Approx (2.0).epsilon (0.02));

        // Across the overlap (seconds 3 to 4) it is never louder than one clip alone, nor silent.
        audio::Transport transport (edit);
        transport.setPositionSeconds (2.0);
        transport.play();
        rig.process (0.2, 0.0f);
        const auto alone = rig.process (0.6, 0.0f);
        rig.process (0.4, 0.0f);
        const auto across = rig.process (0.6, 0.0f);    // seconds 3.2 to 3.8
        transport.stop();
        CHECK (alone > 0.1f);
        CHECK (across > alone * 0.3f);
        CHECK (across < alone * 1.3f);

        // Apart again.
        clips::move (*second, 16.0, track);
        dispatch();
        CHECK (clips::getFadeOutBeats (*first) == doctest::Approx (0.5).epsilon (0.01));
        CHECK (clips::getFadeInBeats (*second) == doctest::Approx (0.0).epsilon (0.01));

        // Turned off for both: overlapped, they keep their own fades (and play one over the other).
        audio_clips::setCrossfades (*first, false);
        audio_clips::setCrossfades (*second, false);
        clips::move (*second, 6.0, track);
        dispatch();
        CHECK_FALSE (audio_clips::hasCrossfades (*first));
        CHECK (clips::getFadeOutBeats (*first) == doctest::Approx (0.5).epsilon (0.01));
        CHECK (clips::getFadeInBeats (*second) == doctest::Approx (0.0).epsilon (0.01));
    }
}
