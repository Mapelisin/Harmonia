#include <doctest.h>

#include "TestAudioRig.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Sends.h"
#include "audio/Transport.h"
#include "core/Project.h"
#include "core/Tracks.h"

#include <set>

using namespace harmonia;
namespace sends = audio::sends;
namespace effects = audio::effects;
using sends::Preset;

namespace
{
    te::AudioTrack& addReturn (core::Project& project, Preset preset)
    {
        juce::String error;
        auto* track = sends::addReturnTrack (project, preset, error);
        REQUIRE_MESSAGE (track != nullptr, error.toStdString());
        CHECK (error.isEmpty());
        return *track;
    }

    juce::StringArray effectNames (te::AudioTrack& track)
    {
        juce::StringArray names;

        for (auto* plugin : effects::get (track.pluginList))
            names.add (effects::getDisplayName (*plugin));

        return names;
    }

    /** An audio track playing a 220 Hz tone for four seconds from the start. */
    te::AudioTrack& addToneTrack (core::Project& project, const test::TempFolder& temp, const juce::String& name)
    {
        auto* track = project.addTrack (name);
        REQUIRE (track != nullptr);
        core::tracks::setKind (*track, core::tracks::Kind::audio);

        juce::String error;
        const auto file = test::writeTone (temp.getFile (name + ".wav"), 4.0, 0.25f);
        REQUIRE (project.addAudioClip (*track, file, 0.0, error) != nullptr);
        return *track;
    }

    float exportRms (core::Project& project)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (project.getEdit(), file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);

        // The first three seconds: the tone, without the silent tail.
        const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 3.0));
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
        reader->read (&buffer, 0, length, 0, true, true);
        return buffer.getRMSLevel (0, 0, length);
    }
}

TEST_SUITE ("audio::sends")
{
    TEST_CASE ("return tracks get a bus each, stay out of the song's tracks and are solo-safe")
    {
        auto project = core::Project::createEmpty (test::session());
        project->clearUndoHistory();
        auto& song = *project->getTracks().getFirst();

        auto& reverb = addReturn (*project, Preset::reverb);
        auto& delay = addReturn (*project, Preset::delay);
        auto& reverb2 = addReturn (*project, Preset::reverb);

        CHECK (project->getTracks().size() == 1);
        REQUIRE (project->getReturnTracks().size() == 3);
        CHECK (project->getReturnTracks()[0] == &reverb);
        CHECK (reverb.getName() == "Reverb");
        CHECK (delay.getName() == "Delay");
        CHECK (reverb2.getName() == "Reverb 2");
        CHECK (core::tracks::isReturn (reverb));
        CHECK_FALSE (core::tracks::isReturn (song));
        CHECK (sends::getBus (song) == -1);

        std::set<int> busses { sends::getBus (reverb), sends::getBus (delay), sends::getBus (reverb2) };
        CHECK (busses.size() == 3);
        CHECK (*busses.begin() >= 0);

        CHECK (reverb.isSoloIsolate (false));
        CHECK (effectNames (reverb) == juce::StringArray ("Reverb"));
        CHECK (effectNames (delay) == juce::StringArray ("Delay"));

        // The effect gives only the effect: the dry sound comes from the tracks themselves.
        auto* reverbPlugin = dynamic_cast<te::ReverbPlugin*> (effects::get (reverb.pluginList).getFirst());
        REQUIRE (reverbPlugin != nullptr);
        CHECK (reverbPlugin->dryParam->getCurrentValue() == doctest::Approx (0.0f));
        auto* delayPlugin = dynamic_cast<te::DelayPlugin*> (effects::get (delay.pluginList).getFirst());
        REQUIRE (delayPlugin != nullptr);
        CHECK (delayPlugin->mixProportion->getCurrentValue() == doctest::Approx (1.0f));

        // New song tracks go above the return tracks.
        auto* added = project->addTrack ("Bass");
        REQUIRE (added != nullptr);
        CHECK (project->getTracks().getLast() == added);
        CHECK (te::getAudioTracks (project->getEdit()).getLast() == &reverb2);

        // Each return track was one undo step.
        project->undo();   // the Bass track was added without a gesture: goes with the last return
        CHECK (project->getReturnTracks().size() == 2);
        project->undo();
        project->undo();
        CHECK (project->getReturnTracks().isEmpty());
        project->redo();
        CHECK (project->getReturnTracks().size() == 1);
    }

    TEST_CASE ("a send sits after the fader, isn't an effect, and one drag is one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        auto& reverb = addReturn (*project, Preset::reverb);
        project->clearUndoHistory();
        const auto effectsBefore = effectNames (track);

        CHECK (sends::getSend (track, reverb) == nullptr);
        CHECK (sends::getLevel (track, reverb) == 0.0f);
        CHECK (sends::getLevelDb (track, reverb) <= -100.0f);

        // Moving a send bar that is at nothing to nothing adds nothing.
        CHECK (sends::setLevel (track, reverb, 0.0f));
        CHECK (sends::getSend (track, reverb) == nullptr);

        project->beginGesture ("Send to Reverb");
        CHECK (sends::setLevelDb (track, reverb, -12.0f));
        CHECK (sends::setLevelDb (track, reverb, -6.0f));
        auto* send = sends::getSend (track, reverb);
        REQUIRE (send != nullptr);
        CHECK (sends::getLevelDb (track, reverb) == doctest::Approx (-6.0f).epsilon (0.01));
        CHECK (send->busNumber.get() == sends::getBus (reverb));

        const auto sendIndex = track.pluginList.indexOf (send);
        CHECK (sendIndex == track.pluginList.indexOf (track.getVolumePlugin()) + 1);
        CHECK (sendIndex < track.pluginList.indexOf (track.getLevelMeterPlugin()));
        CHECK (effectNames (track) == effectsBefore);

        // A return track doesn't send (it could feed itself).
        CHECK_FALSE (sends::setLevel (reverb, reverb, 0.5f));

        project->undo();
        CHECK (sends::getSend (track, reverb) == nullptr);
        project->redo();
        CHECK (sends::getLevelDb (track, reverb) == doctest::Approx (-6.0f).epsilon (0.01));
    }

    TEST_CASE ("effects on a return track go after its input")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& reverb = addReturn (*project, Preset::empty);
        CHECK (reverb.getName() == "Return");
        CHECK (effectNames (reverb).isEmpty());

        juce::String error;
        effects::EffectType eq;

        for (const auto& type : effects::getBuiltIn())
            if (type.name == "EQ")
                eq = type;

        auto* added = effects::add (reverb.pluginList, eq, error, 0);
        REQUIRE (added != nullptr);
        CHECK (dynamic_cast<te::AuxReturnPlugin*> (reverb.pluginList[0]) != nullptr);
        CHECK (reverb.pluginList.indexOf (added) == 1);
        CHECK (effectNames (reverb) == juce::StringArray ("EQ"));
    }

    TEST_CASE ("deleting a return track takes the sends to it along; undo brings both back")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        auto& reverb = addReturn (*project, Preset::reverb);
        auto& delay = addReturn (*project, Preset::delay);
        sends::setLevelDb (track, reverb, -3.0f);
        sends::setLevelDb (track, delay, -9.0f);
        project->clearUndoHistory();

        sends::removeReturnTrack (*project, reverb);
        REQUIRE (project->getReturnTracks().size() == 1);
        CHECK (project->getReturnTracks()[0] == &delay);
        CHECK (sends::getSend (track, delay) != nullptr);

        int sendsLeft = 0;

        for (auto* plugin : track.pluginList)
            if (dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr)
                ++sendsLeft;

        CHECK (sendsLeft == 1);

        project->undo();
        REQUIRE (project->getReturnTracks().size() == 2);
        auto& restored = *project->getReturnTracks()[0];
        CHECK (restored.getName() == "Reverb");
        CHECK (sends::getLevelDb (track, restored) == doctest::Approx (-3.0f).epsilon (0.01));

        // A new return never picks up a bus that some track still sends to.
        auto& another = addReturn (*project, Preset::empty);
        CHECK (sends::getBus (another) != sends::getBus (restored));
        CHECK (sends::getBus (another) != sends::getBus (delay));
        CHECK (sends::getSend (track, another) == nullptr);
    }

    TEST_CASE ("renaming a return track names its bus too")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& reverb = addReturn (*project, Preset::reverb);
        project->clearUndoHistory();

        sends::renameReturnTrack (*project, reverb, "  Hall  ");
        CHECK (reverb.getName() == "Hall");
        CHECK (project->getEdit().getAuxBusName (sends::getBus (reverb)) == "Hall");

        sends::renameReturnTrack (*project, reverb, "   ");
        CHECK (reverb.getName() == "Hall");

        project->undo();
        CHECK (reverb.getName() == "Reverb");
    }

    TEST_CASE ("return tracks and sends are saved with the project")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        auto& delay = addReturn (*project, Preset::delay);
        sends::setLevelDb (track, delay, -4.0f);

        const auto file = temp.getFile ("Song.harmonia");
        REQUIRE (project->save (file).isEmpty());

        juce::String error;
        auto loaded = core::Project::load (test::session(), file, error);
        REQUIRE (loaded != nullptr);
        REQUIRE (loaded->getTracks().size() == 1);
        REQUIRE (loaded->getReturnTracks().size() == 1);
        auto& loadedDelay = *loaded->getReturnTracks()[0];
        CHECK (loadedDelay.getName() == "Delay");
        CHECK (loadedDelay.isSoloIsolate (false));
        CHECK (effectNames (loadedDelay) == juce::StringArray ("Delay"));
        CHECK (sends::getLevelDb (*loaded->getTracks()[0], loadedDelay) == doctest::Approx (-4.0f).epsilon (0.01));
    }

    TEST_CASE ("the export plays what the tracks send through the return tracks")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->getEdit().deleteTrack (project->getTracks().getFirst());
        auto& tone = addToneTrack (*project, temp, "Tone");
        const auto dry = exportRms (*project);
        REQUIRE (dry > 0.05f);

        // An empty return plays the track once more: twice as loud.
        auto& copy = addReturn (*project, Preset::empty);
        CHECK (exportRms (*project) == doctest::Approx (dry).epsilon (0.02));   // nothing sent yet
        sends::setLevelDb (tone, copy, 0.0f);
        CHECK (exportRms (*project) == doctest::Approx (2.0f * dry).epsilon (0.05));

        // A muted return takes the copy away.
        copy.setMute (true);
        CHECK (exportRms (*project) == doctest::Approx (dry).epsilon (0.02));
        copy.setMute (false);

        // With only the return left unmuted there's nothing to export.
        tone.setMute (true);
        juce::TemporaryFile file (".wav");
        CHECK (audio::exportWav (project->getEdit(), file.getFile()).startsWith ("Nothing to export"));
    }

    TEST_CASE ("in playback a muted track sends nothing; soloing a track keeps its return")
    {
        test::AudioRig rig;
        auto& edit = rig.project->getEdit();
        edit.deleteTrack (rig.project->getTracks().getFirst());
        auto& tone = addToneTrack (*rig.project, rig.temp, "Tone");
        auto& other = rig.addAudioTrack ("Other");
        auto& copy = addReturn (*rig.project, Preset::empty);
        edit.dispatchPendingUpdatesSynchronously();

        audio::Transport transport (edit);
        transport.play();
        rig.process (0.2, 0.0f);
        const auto dry = rig.process (0.5, 0.0f);
        REQUIRE (dry > 0.05f);

        const auto settle = [&rig, &edit]
        {
            edit.dispatchPendingUpdatesSynchronously();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            rig.process (0.2, 0.0f);
        };

        sends::setLevelDb (tone, copy, 0.0f);
        settle();
        CHECK (rig.process (0.5, 0.0f) == doctest::Approx (2.0f * dry).epsilon (0.05));

        tone.setMute (true);
        settle();
        CHECK (rig.process (0.5, 0.0f) < 0.001f);   // the return is silent too
        tone.setMute (false);

        other.setSolo (true);
        settle();
        CHECK (rig.process (0.5, 0.0f) < 0.001f);   // soloing another track silences the send

        other.setSolo (false);
        tone.setSolo (true);
        settle();
        CHECK (rig.process (0.5, 0.0f) == doctest::Approx (2.0f * dry).epsilon (0.05));   // the return stays

        transport.stop();
    }
}
