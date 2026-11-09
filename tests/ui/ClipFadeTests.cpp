#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/arrangement/ArrangementGestures.h"

using namespace harmonia;
using namespace harmonia::ui::arrangement;
namespace clips = core::clips;
namespace audio_handles = harmonia::ui::audio_handles;

namespace
{
    constexpr int left = juce::ModifierKeys::leftButtonModifier;

    void dispatch (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** An audio track with a 4-second tone (8 beats at 120 BPM) at the start, the arrangement at
        16 pixels per beat. */
    struct FadeFixture
    {
        FadeFixture()
        {
            project->getEdit().deleteTrack (project->getTracks().getFirst());
            track = model.addAudioTrack();
            REQUIRE (track != nullptr);

            juce::String error;
            const auto file = test::writeTone (temp.getFile ("Tone.wav"), 4.0, 0.25f);
            clip = dynamic_cast<te::AudioClipBase*> (project->addAudioClip (*track, file, 0.0, error));
            REQUIRE (clip != nullptr);

            view.width = 1000.0f;
            view.height = 400.0f;
            view.pixelsPerBeat = 16.0;
            view.numTracks = model.getNumTracks();
            model.setPixelsPerBeat (view.pixelsPerBeat);
            dispatch();
            project->clearUndoHistory();
        }

        audio_handles::Handles handles()
        {
            for (const auto& d : model.getClips())
                if (d.clip == clip)
                    if (const auto h = ArrangementGestures::getAudioHandles (d, view))
                        return *h;

            FAIL ("the clip has no handles");
            return {};
        }

        void drag (juce::Point<float> from, juce::Point<float> to, int clicks = 1)
        {
            const juce::ModifierKeys keys (left);
            gestures.down ({ from, keys, clicks });

            for (int i = 1; i <= 10; ++i)
                gestures.drag ({ from + (to - from) * ((float) i / 10.0f), keys, clicks });

            gestures.up ({ to, keys, clicks });
        }

        void doubleClick (juce::Point<float> at)
        {
            const juce::ModifierKeys keys (left);
            gestures.down ({ at, keys, 1 });
            gestures.up ({ at, keys, 1 });
            gestures.down ({ at, keys, 2 });
            gestures.up ({ at, keys, 2 });
        }

        test::TempFolder temp;   // first: deleted last
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        ArrangementModel model { *project };
        ArrangementView view;
        ArrangementGestures gestures { model, view };
        te::AudioTrack* track = nullptr;
        te::AudioClipBase* clip = nullptr;
    };

    /** RMS of the exported song in each time range (seconds). */
    std::vector<float> exportRms (core::Project& project, std::initializer_list<juce::Range<double>> ranges)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (project.getEdit(), file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);

        std::vector<float> result;

        for (const auto& range : ranges)
        {
            const auto start = (juce::int64) (range.getStart() * reader->sampleRate);
            const auto length = (int) (range.getLength() * reader->sampleRate);
            REQUIRE (start + length <= reader->lengthInSamples);

            juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
            reader->read (&buffer, 0, length, start, true, true);
            result.push_back (buffer.getRMSLevel (0, 0, length));
        }

        return result;
    }
}

TEST_SUITE ("audio clips: fades and gain")
{
    TEST_CASE ("fades are set in beats, can't run into each other, and are one undo step each")
    {
        FadeFixture f;
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (0.0));
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (0.0));

        f.project->beginGesture ("Fade in");
        clips::setFadeInBeats (*f.clip, 2.0);
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (2.0));
        CHECK (f.clip->getFadeIn().inSeconds() == doctest::Approx (1.0));

        f.project->beginGesture ("Fade out");
        clips::setFadeOutBeats (*f.clip, 10.0);    // only the room the fade in leaves
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (6.0).epsilon (0.001));
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (2.0));

        f.project->beginGesture ("Fade out");
        clips::setFadeOutBeats (*f.clip, 1.5);
        clips::setFadeInBeats (*f.clip, -3.0);
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (0.0));
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (1.5));

        f.project->undo();
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (2.0));
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (6.0).epsilon (0.001));
        f.project->undo();
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (0.0));
    }

    TEST_CASE ("shapes and gain")
    {
        FadeFixture f;
        CHECK (clips::getFadeShape (*f.clip) == clips::FadeShape::linear);

        for (auto shape : clips::getFadeShapes())
        {
            clips::setFadeShape (*f.clip, shape);
            CHECK (clips::getFadeShape (*f.clip) == shape);
            CHECK (clips::getFadeShapeName (shape).isNotEmpty());
        }

        clips::setGainDb (*f.clip, -4.5f);
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (-4.5f));
        clips::setGainDb (*f.clip, 30.0f);
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (clips::maxGainDb));
        clips::setGainDb (*f.clip, -500.0f);
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (clips::silentGainDb));
    }

    TEST_CASE ("splitting keeps the fade in on the first half and the fade out on the second")
    {
        FadeFixture f;
        clips::setFadeInBeats (*f.clip, 1.0);
        clips::setFadeOutBeats (*f.clip, 2.0);
        clips::setGainDb (*f.clip, -3.0f);

        auto* second = dynamic_cast<te::AudioClipBase*> (clips::split (*f.clip, 4.0));
        REQUIRE (second != nullptr);
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (1.0));
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (0.0));
        CHECK (clips::getFadeInBeats (*second) == doctest::Approx (0.0));
        CHECK (clips::getFadeOutBeats (*second) == doctest::Approx (2.0));
        CHECK (clips::getGainDb (*second) == doctest::Approx (-3.0f));
    }

    TEST_CASE ("the export plays the gain and the fades")
    {
        FadeFixture f;
        const auto dry = exportRms (*f.project, { { 0.0, 0.25 }, { 1.5, 2.5 }, { 3.75, 4.0 } });
        REQUIRE (dry[1] > 0.05f);
        CHECK (dry[0] == doctest::Approx (dry[1]).epsilon (0.05));

        clips::setGainDb (*f.clip, -6.0f);
        const auto quieter = exportRms (*f.project, { { 1.5, 2.5 } });
        CHECK (quieter[0] == doctest::Approx (dry[1] * juce::Decibels::decibelsToGain (-6.0f)).epsilon (0.03));

        clips::setGainDb (*f.clip, 0.0f);
        clips::setFadeInBeats (*f.clip, 2.0);      // the first second
        clips::setFadeOutBeats (*f.clip, 2.0);     // the last second
        const auto faded = exportRms (*f.project, { { 0.0, 0.25 }, { 1.5, 2.5 }, { 3.75, 4.0 } });
        CHECK (faded[0] < 0.25f * dry[0]);
        CHECK (faded[1] == doctest::Approx (dry[1]).epsilon (0.03));
        CHECK (faded[2] < 0.25f * dry[2]);
    }

    TEST_CASE ("dragging the handles makes fades and sets the gain; double-click resets; one undo step each")
    {
        FadeFixture f;
        auto h = f.handles();
        CHECK (h.fadeIn.x < h.gain.x);
        CHECK (h.gain.x < h.fadeOut.x);

        CHECK (f.gestures.hitTest (h.fadeIn)->second == ArrangementGestures::Zone::fadeIn);
        CHECK (f.gestures.hitTest (h.fadeOut)->second == ArrangementGestures::Zone::fadeOut);
        CHECK (f.gestures.hitTest (h.gain)->second == ArrangementGestures::Zone::gain);

        // Below the handles the edges still resize the clip.
        CHECK (f.gestures.hitTest ({ h.body.getX() + 1.0f, h.body.getBottom() - 3.0f })->second == ArrangementGestures::Zone::startEdge);

        // Fade in: two beats (32 pixels), free of the grid.
        f.drag (h.fadeIn, { f.view.beatToX (2.0), h.fadeIn.y });
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (2.0).epsilon (0.01));

        h = f.handles();
        f.drag (h.fadeOut, { f.view.beatToX (5.5), h.fadeOut.y });
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (2.5).epsilon (0.01));

        // Gain: down is quieter.
        h = f.handles();
        f.drag (h.gain, h.gain + juce::Point<float> (0.0f, 8.0f));
        const auto lowered = clips::getGainDb (*f.clip);
        CHECK (lowered < -2.0f);
        CHECK (audio_handles::gainToY (lowered, h.body) == doctest::Approx (h.gain.y + 8.0f).epsilon (0.05));

        f.project->undo();
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (0.0f));
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (2.5).epsilon (0.01));

        // Double-click a handle: back to none.
        f.doubleClick (f.handles().fadeIn);
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (0.0));

        h = f.handles();
        f.drag (h.gain, h.gain + juce::Point<float> (0.0f, 10.0f));
        CHECK (clips::getGainDb (*f.clip) < -2.0f);
        f.doubleClick (f.handles().gain);
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (0.0f));
    }

    TEST_CASE ("gain and height in the clip go both ways; very narrow clips have no handles")
    {
        const juce::Rectangle<float> body (0.0f, 20.0f, 100.0f, 38.0f);

        for (const auto db : { -24.0f, -6.0f, 0.0f, 3.0f, 6.0f })
            CHECK (audio_handles::yToGain (audio_handles::gainToY (db, body), body) == doctest::Approx (db).epsilon (0.01));

        CHECK (audio_handles::yToGain (body.getBottom() + 10.0f, body) == clips::silentGainDb);
        CHECK (audio_handles::yToGain (body.getY() - 10.0f, body) == doctest::Approx (clips::maxGainDb));

        FadeFixture f;
        f.view.pixelsPerBeat = 4.0;   // the clip is 32 pixels wide

        for (const auto& d : f.model.getClips())
            CHECK_FALSE (ArrangementGestures::getAudioHandles (d, f.view).has_value());
    }
}
