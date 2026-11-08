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
