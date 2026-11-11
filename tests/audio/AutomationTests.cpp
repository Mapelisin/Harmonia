#include <doctest.h>

#include "TestAudioRig.h"
#include "audio/Automation.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Sends.h"
#include "audio/Transport.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace automation = audio::automation;
namespace effects = audio::effects;

namespace
{
    te::AutomatableParameter& volumeOf (te::AudioTrack& track)
    {
        return *track.getVolumePlugin()->volParam;
    }

    float zeroDb()
    {
        return te::decibelsToVolumeFaderPosition (0.0f);
    }

    juce::StringArray namesOf (te::AudioTrack& track)
    {
        juce::StringArray names;

        for (auto* parameter : automation::getParameters (track))
            names.add (automation::getDisplayName (*parameter));

        return names;
    }

    te::AudioTrack& addToneTrack (core::Project& project, const test::TempFolder& temp, double seconds)
    {
        auto* track = project.addTrack ("Tone");
        REQUIRE (track != nullptr);
        core::tracks::setKind (*track, core::tracks::Kind::audio);

        juce::String error;
        const auto file = test::writeTone (temp.getFile ("Tone.wav"), seconds, 0.25f);
        REQUIRE (project.addAudioClip (*track, file, 0.0, error) != nullptr);
        return *track;
    }

    /** RMS of the exported song between two times (seconds). */
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

TEST_SUITE ("audio::automation")
{
    TEST_CASE ("a track offers volume, pan, its sends, then its effects' knobs")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        CHECK (namesOf (track) == juce::StringArray ("Volume", "Pan"));

        juce::String error;
        auto* reverb = audio::sends::addReturnTrack (*project, audio::sends::Preset::reverb, error);
        REQUIRE (reverb != nullptr);
        audio::sends::setLevelDb (track, *reverb, -6.0f);

        effects::EffectType filter;

        for (const auto& type : effects::getBuiltIn())
            if (type.name == "Filter")
                filter = type;

        REQUIRE (effects::add (track.pluginList, filter, error) != nullptr);

        const auto names = namesOf (track);
        REQUIRE (names.size() > 3);
        CHECK (names[0] == "Volume");
        CHECK (names[1] == "Pan");
        CHECK (names[2] == "Send: Reverb");
        CHECK (names[3].startsWith ("Low-pass: "));

        auto* firstFilterParameter = automation::getParameters (track)[3];
        CHECK (automation::getGroupName (*firstFilterParameter) == "Low-pass");
        CHECK (automation::getGroupName (volumeOf (track)).isEmpty());

        // A value as the knob shows it.
        CHECK (automation::valueToText (volumeOf (track), zeroDb()).contains ("0.0"));
        CHECK (automation::valueToText (volumeOf (track), zeroDb()).contains ("dB"));
    }

    TEST_CASE ("points are added in time order, move between their neighbours and are one undo step per gesture")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        auto& volume = volumeOf (track);
        project->clearUndoHistory();

        CHECK_FALSE (automation::hasPoints (volume));
        CHECK (automation::getAutomated (track).isEmpty());

        project->beginGesture ("Add automation point");
        CHECK (automation::addPoint (volume, 8.0, 0.25f) == 0);
        CHECK (automation::addPoint (volume, 0.0, 1.0f) == 0);       // before the first
        CHECK (automation::addPoint (volume, 4.0, 0.5f) == 1);       // between
        CHECK (automation::addPoint (volume, -3.0, 2.0f) == 1);      // clamped to beat 0 (after the one there) and to the top

        auto points = automation::getPoints (volume);
        REQUIRE (points.size() == 4);
        CHECK (points[0].beat == doctest::Approx (0.0));
        CHECK (points[0].value == doctest::Approx (1.0f));
        CHECK (points[2].beat == doctest::Approx (4.0));
        CHECK (points[2].value == doctest::Approx (0.5f));
        CHECK (points[3].beat == doctest::Approx (8.0));
        CHECK (automation::getAutomated (track).size() == 1);

        // Straight lines between points, flat after the last.
        CHECK (automation::getValueAt (volume, 6.0) == doctest::Approx (0.375f).epsilon (0.001));
        CHECK (automation::getValueAt (volume, 20.0) == doctest::Approx (0.25f));

        // One drag = one step, and a point can't pass its neighbours.
        project->beginGesture ("Move automation point");
        automation::movePoint (volume, 2, 5.0, 0.7f);
        automation::movePoint (volume, 2, 12.0, 0.8f);
        points = automation::getPoints (volume);
        CHECK (points[2].beat == doctest::Approx (8.0));     // stopped at the next point
        CHECK (points[2].value == doctest::Approx (0.8f));

        project->undo();
        points = automation::getPoints (volume);
        CHECK (points[2].beat == doctest::Approx (4.0));
        CHECK (points[2].value == doctest::Approx (0.5f));

        project->beginGesture ("Delete automation point");
        automation::removePoint (volume, 1);
        automation::removePoint (volume, 99);                // nothing there: ignored
        CHECK (automation::getPoints (volume).size() == 3);

        project->beginGesture ("Clear automation");
        automation::clear (volume);
        CHECK_FALSE (automation::hasPoints (volume));

        project->undo();
        CHECK (automation::getPoints (volume).size() == 3);
        project->undo();
        CHECK (automation::getPoints (volume).size() == 4);
        project->undo();
        CHECK_FALSE (automation::hasPoints (volume));
    }

    TEST_CASE ("points stay on their beats when the tempo changes, and are saved with the project")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        automation::addPoint (*track.getVolumePlugin()->panParam, 4.0, 0.0f);
        automation::addPoint (*track.getVolumePlugin()->panParam, 8.0, 1.0f);

        project->setTempoBpm (90.0);
        auto points = automation::getPoints (*track.getVolumePlugin()->panParam);
        REQUIRE (points.size() == 2);
        CHECK (points[0].beat == doctest::Approx (4.0).epsilon (0.001));
        CHECK (points[1].beat == doctest::Approx (8.0).epsilon (0.001));

        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());

        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE (loaded != nullptr);
        auto& loadedTrack = *loaded->getTracks().getFirst();
        const auto loadedPoints = automation::getPoints (*loadedTrack.getVolumePlugin()->panParam);
        REQUIRE (loadedPoints.size() == 2);
        CHECK (loadedPoints[0].beat == doctest::Approx (4.0).epsilon (0.001));
        CHECK (loadedPoints[1].value == doctest::Approx (1.0f));
        CHECK (automation::getAutomated (loadedTrack).size() == 1);
    }

    TEST_CASE ("the export follows a volume line")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->getEdit().deleteTrack (project->getTracks().getFirst());
        auto& tone = addToneTrack (*project, temp, 4.0);

        const auto dry = exportRms (*project, { { 0.2, 1.8 }, { 2.2, 3.8 } });
        REQUIRE (dry[0] > 0.05f);
        CHECK (dry[1] == doctest::Approx (dry[0]).epsilon (0.02));

        // Full volume for the first two seconds (beats 0..4 at 120 BPM), then silence.
        auto& volume = volumeOf (tone);
        automation::addPoint (volume, 0.0, zeroDb());
        automation::addPoint (volume, 4.0, zeroDb());
        automation::addPoint (volume, 4.1, 0.0f);

        const auto ridden = exportRms (*project, { { 0.2, 1.8 }, { 2.2, 3.8 } });
        CHECK (ridden[0] == doctest::Approx (dry[0]).epsilon (0.02));
        CHECK (ridden[1] < 0.001f);

        // Without the line the track plays as before.
        automation::clear (volume);
        const auto cleared = exportRms (*project, { { 2.2, 3.8 } });
        CHECK (cleared[0] == doctest::Approx (dry[1]).epsilon (0.02));
    }

    TEST_CASE ("a stretch of the line bends, late or soon; its handle is the middle of the bend; saved with the project")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& volume = volumeOf (*project->getTracks().getFirst());

        automation::addPoint (volume, 0.0, 0.2f);
        automation::addPoint (volume, 8.0, 0.8f);       // a rise
        automation::addPoint (volume, 12.0, 0.8f);      // level
        automation::addPoint (volume, 12.0, 0.4f);      // a jump
        automation::addPoint (volume, 20.0, 0.1f);      // a fall
        project->clearUndoHistory();

        // Straight: half way up in the middle, and the handle is there.
        CHECK (automation::getValueAt (volume, 4.0) == doctest::Approx (0.5f));
        auto handle = automation::getBendHandle (volume, 0);
        REQUIRE (handle.has_value());
        CHECK (handle->beat == doctest::Approx (4.0));
        CHECK (handle->value == doctest::Approx (0.5f));
        CHECK (handle->curve == 0.0f);

        // Nothing to bend where the line is level, jumps or ends.
        CHECK_FALSE (automation::getBendHandle (volume, 1).has_value());
        CHECK_FALSE (automation::getBendHandle (volume, 2).has_value());
        CHECK (automation::getBendHandle (volume, 3).has_value());
        CHECK_FALSE (automation::getBendHandle (volume, 4).has_value());
        CHECK_FALSE (automation::getBendHandle (volume, -1).has_value());

        // Late: the value holds, then goes. The line still rises all the way, between the same ends.
        project->beginGesture ("Bend");
        automation::setCurve (volume, 0, 0.5f);
        CHECK (automation::getPoints (volume)[0].curve == 0.5f);
        CHECK (automation::getValueAt (volume, 0.0) == doctest::Approx (0.2f));
        CHECK (automation::getValueAt (volume, 8.0) == doctest::Approx (0.8f));
        CHECK (automation::getValueAt (volume, 4.0) < 0.4f);

        for (double beat = 0.5; beat <= 8.0; beat += 0.5)
            CHECK (automation::getValueAt (volume, beat) >= automation::getValueAt (volume, beat - 0.5));

        // The handle: half of the way from the middle to the corner, and on the line.
        handle = automation::getBendHandle (volume, 0);
        REQUIRE (handle.has_value());
        CHECK (handle->value == doctest::Approx (0.35f));
        CHECK (handle->beat == doctest::Approx (6.0));
        CHECK (handle->curve == 0.5f);
        CHECK (automation::getValueAt (volume, handle->beat) == doctest::Approx (handle->value).epsilon (0.001));

        // The bend that puts the handle at a value: what dragging it there asks for.
        CHECK (automation::getCurveForHandle (volume, 0, 0.35f) == doctest::Approx (0.5f));
        CHECK (automation::getCurveForHandle (volume, 0, 0.65f) == doctest::Approx (-0.5f));
        CHECK (automation::getCurveForHandle (volume, 0, 0.5f) == doctest::Approx (0.0f));
        CHECK (automation::getCurveForHandle (volume, 0, 0.0f) == 1.0f);
        CHECK (automation::getCurveForHandle (volume, 0, 1.0f) == -1.0f);
        CHECK (automation::getCurveForHandle (volume, 1, 0.3f) == 0.0f);       // level: nothing to ask

        // A fall bends the same way: late holds the value up.
        automation::setCurve (volume, 3, 0.5f);
        CHECK (automation::getValueAt (volume, 16.0) > 0.3f);
        CHECK (automation::getBendHandle (volume, 3)->value == doctest::Approx (0.325f));
        CHECK (automation::getCurveForHandle (volume, 3, 0.325f) == doctest::Approx (0.5f));
        CHECK (automation::getCurveForHandle (volume, 3, 0.175f) == doctest::Approx (-0.5f));

        // All the way: a step at the end, or at the start.
        automation::setCurve (volume, 0, 5.0f);
        CHECK (automation::getPoints (volume)[0].curve == 1.0f);
        CHECK (automation::getValueAt (volume, 7.0) == doctest::Approx (0.2f).epsilon (0.01));
        automation::setCurve (volume, 0, -1.0f);
        CHECK (automation::getValueAt (volume, 1.0) == doctest::Approx (0.8f).epsilon (0.01));

        // One undo step for the lot.
        project->undo();
        CHECK (automation::getPoints (volume)[0].curve == 0.0f);
        CHECK (automation::getPoints (volume)[3].curve == 0.0f);
        project->redo();
        CHECK (automation::getPoints (volume)[0].curve == -1.0f);

        // A point put on a bent stretch bends on as it did; moved, it keeps its bend.
        automation::setCurve (volume, 0, 0.5f);
        CHECK (automation::addPoint (volume, 4.0, 0.3f) == 1);
        CHECK (automation::getPoints (volume)[0].curve == 0.5f);
        CHECK (automation::getPoints (volume)[1].curve == 0.5f);
        automation::movePoint (volume, 1, 5.0, 0.35f);
        CHECK (automation::getPoints (volume)[1] == automation::Point { 5.0, 0.35f, 0.5f });

        // The bends are in the song's file.
        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());

        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE (loaded != nullptr);
        const auto loadedPoints = automation::getPoints (volumeOf (*loaded->getTracks().getFirst()));
        REQUIRE (loadedPoints.size() == 6);
        CHECK (loadedPoints[0].curve == 0.5f);
        CHECK (loadedPoints[1].curve == 0.5f);
        CHECK (loadedPoints[4].curve == 0.5f);
        CHECK (loadedPoints[2].curve == 0.0f);
    }

    TEST_CASE ("the export follows a bent line: what is heard is what the line shows")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->getEdit().deleteTrack (project->getTracks().getFirst());
        auto& tone = addToneTrack (*project, temp, 4.0);
        auto& volume = volumeOf (tone);

        // Moments of the song (seconds; 120 BPM: two beats a second), a twentieth of a second each.
        const std::vector<double> moments { 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5 };
        const auto listen = [&]
        {
            std::vector<float> heard;

            for (const auto moment : moments)
                heard.push_back (exportRms (*project, { { moment - 0.025, moment + 0.025 } })[0]);

            return heard;
        };

        // A fade from full volume to silence over the four seconds.
        automation::addPoint (volume, 0.0, zeroDb());
        automation::addPoint (volume, 8.0, 0.0f);
        const auto full = exportRms (*project, { { 0.0, 0.02 } })[0];
        REQUIRE (full > 0.05f);

        const auto expected = [&] (double moment)
        {
            return full * te::volumeFaderPositionToGain (automation::getValueAt (volume, 2.0 * moment) * 1.0f);
        };

        for (const auto bend : { 0.0f, 0.3f, 0.5f, 0.8f, -0.5f, -0.8f })
        {
            CAPTURE (bend);
            automation::setCurve (volume, 0, bend);
            const auto heard = listen();

            for (size_t i = 0; i < moments.size(); ++i)
            {
                CAPTURE (moments[i]);
                CHECK (heard[i] == doctest::Approx (expected (moments[i])).epsilon (0.08));
            }
        }

        // And the bends are heard apart: late is louder in the middle than straight, soon is quieter.
        automation::setCurve (volume, 0, 0.5f);
        const auto late = listen()[3];
        automation::setCurve (volume, 0, 0.0f);
        const auto straight = listen()[3];
        automation::setCurve (volume, 0, -0.5f);
        const auto soon = listen()[3];
        CHECK (late > 1.5f * straight);
        CHECK (soon < 0.67f * straight);
    }

    TEST_CASE ("playback follows a volume line")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        edit.deleteTrack (rig.project->getTracks().getFirst());
        auto& tone = addToneTrack (*rig.project, rig.temp, 4.0);

        auto& volume = volumeOf (tone);
        automation::addPoint (volume, 0.0, zeroDb());
        automation::addPoint (volume, 2.0, zeroDb());
        automation::addPoint (volume, 2.1, 0.0f);   // silent from one second on
        edit.dispatchPendingUpdatesSynchronously();

        audio::Transport transport (edit);
        transport.play();
        rig.process (0.1, 0.0f);
        CHECK (rig.process (0.6, 0.0f) > 0.05f);
        rig.process (0.6, 0.0f);
        CHECK (rig.process (0.6, 0.0f) < 0.001f);

        // Without the line it plays at its fader again, right away.
        automation::clear (volume);
        rig.process (0.2, 0.0f);
        CHECK (rig.process (0.5, 0.0f) > 0.05f);
        transport.stop();
    }
}
