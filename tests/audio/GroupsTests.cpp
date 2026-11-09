#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Groups.h"
#include "audio/Sends.h"
#include "audio/Sidechain.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace groups = audio::groups;
namespace sidechain = audio::sidechain;

namespace
{
    /** An audio track playing `file` from the start. */
    te::AudioTrack& addAudioTrack (core::Project& project, const juce::File& file, const juce::String& name)
    {
        auto* track = project.addTrack (name);
        REQUIRE (track != nullptr);
        core::tracks::setKind (*track, core::tracks::Kind::audio);

        juce::String error;
        REQUIRE_MESSAGE (project.addAudioClip (*track, file, 0.0, error) != nullptr, error.toStdString());
        return *track;
    }

    /** A track playing a steady 220 Hz tone for four seconds. */
    te::AudioTrack& addToneTrack (core::Project& project, const test::TempFolder& temp, const juce::String& name)
    {
        return addAudioTrack (project, test::writeTone (temp.getFile (name + ".wav"), 4.0, 0.25f), name);
    }

    /** A kick of sorts: a loud 110 Hz hit for the first 0.1 s of every half second, for four seconds. */
    te::AudioTrack& addHitsTrack (core::Project& project, const test::TempFolder& temp, const juce::String& name)
    {
        constexpr double sampleRate = 44100.0;
        const auto file = temp.getFile (name + ".wav");

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (1)
                                                       .withBitsPerSample (16));
        REQUIRE (writer != nullptr);

        const auto numSamples = (int) (4.0 * sampleRate);
        juce::AudioBuffer<float> buffer (1, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto inBeat = std::fmod (i / sampleRate, 0.5);
            buffer.setSample (0, i, inBeat < 0.1 ? 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * i / sampleRate) : 0.0f);
        }

        writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        writer.reset();
        return addAudioTrack (project, file, name);
    }

    struct Rendered
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;

        float getRms (double from, double to) const
        {
            const auto start = juce::jlimit (0, buffer.getNumSamples(), (int) (from * sampleRate));
            const auto end = juce::jlimit (start, buffer.getNumSamples(), (int) (to * sampleRate));
            return end > start ? buffer.getRMSLevel (0, start, end - start) : 0.0f;
        }

        /** How loud the bass is just after the kick hits (beats 1 to 6), against late in those beats. */
        float getDuckRatio() const
        {
            auto early = 0.0f, late = 0.0f;

            for (int beat = 1; beat < 7; ++beat)
            {
                early += getRms (beat * 0.5 + 0.02, beat * 0.5 + 0.09);
                late += getRms (beat * 0.5 + 0.38, beat * 0.5 + 0.48);
            }

            return late > 0.0f ? early / late : 0.0f;
        }
    };

    Rendered read (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        Rendered result;
        result.sampleRate = reader->sampleRate;
        result.buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&result.buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        return result;
    }

    Rendered exportMix (core::Project& project)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (project.getEdit(), file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());
        return read (file.getFile());
    }

    /** The steady part of a tone track's sound in a render (after its start, before its end). */
    float steadyRms (const Rendered& rendered)
    {
        return rendered.getRms (0.5, 3.0);
    }

    Rendered exportStem (core::Project& project, te::AudioTrack& track, const test::TempFolder& temp)
    {
        const auto folder = temp.getFile ("stems-" + juce::Uuid().toString().substring (0, 6));
        const auto result = audio::exportStems (project.getEdit(), folder, "Song");
        REQUIRE_MESSAGE (result.error.isEmpty(), result.error.toStdString());

        for (const auto& file : result.files)
            if (file.getFileName().contains (track.getName()))
                return read (file);

        FAIL ("no stem for " << track.getName());
        return {};
    }

    Rendered renderAlone (te::AudioTrack& track)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::renderTrack (track, file.getFile(), { te::TimePosition(), te::TimePosition::fromSeconds (4.0) });
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());
        return read (file.getFile());
    }

    te::CompressorPlugin& addCompressor (te::PluginList& chain)
    {
        juce::String error;
        auto types = audio::effects::getBuiltIn();
        types.removeIf ([] (const auto& type) { return type.builtInType != te::CompressorPlugin::xmlTypeName; });
        REQUIRE (types.size() == 1);

        auto* compressor = dynamic_cast<te::CompressorPlugin*> (audio::effects::add (chain, types.getFirst(), error));
        REQUIRE_MESSAGE (compressor != nullptr, error.toStdString());
        return *compressor;
    }

    /** A compressor that pushes the sound right down whenever what it listens to gets loud. */
    te::CompressorPlugin& addDucker (te::AudioTrack& track)
    {
        auto* compressor = &addCompressor (track.pluginList);
        compressor->setThreshold (0.02f);
        compressor->setRatio (0.05f);
        compressor->attackMs.setParameter (1.0f, juce::sendNotification);
        compressor->releaseMs.setParameter (60.0f, juce::sendNotification);
        return *compressor;
    }

    constexpr float minus12Db = 0.2512f;
}

TEST_SUITE ("audio::groups")
{
    TEST_CASE ("group tracks stay out of the song's tracks, their tracks play into them, each change is one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& lead = *project->getTracks().getFirst();
        auto& bass = *project->addTrack ("Bass");
        project->clearUndoHistory();

        auto* drums = groups::addGroup (*project, { &lead });
        REQUIRE (drums != nullptr);
        auto* second = groups::addGroup (*project, {}, "Keys");
        REQUIRE (second != nullptr);

        CHECK (project->getTracks() == juce::Array<te::AudioTrack*> { &lead, &bass });
        CHECK (project->getGroupTracks() == juce::Array<te::AudioTrack*> { drums, second });
        CHECK (project->getReturnTracks().isEmpty());
        CHECK (drums->getName() == "Group");
        CHECK (second->getName() == "Keys");
        CHECK (core::tracks::isGroup (*drums));
        CHECK (core::tracks::isBus (*drums));
        CHECK_FALSE (core::tracks::isReturn (*drums));
        CHECK (groups::getGroup (lead) == drums);
        CHECK (groups::getGroup (bass) == nullptr);
        CHECK (groups::getTracks (*project, *drums) == juce::Array<te::AudioTrack*> { &lead });
        CHECK (groups::addGroup (*project, {})->getName() == "Group 2");
        project->undo();

        // Groups and return tracks never play into a group.
        groups::setGroup (*second, drums);
        CHECK (groups::getGroup (*second) == nullptr);

        project->beginGesture ("Bass to the group");
        groups::setGroup (bass, drums);
        CHECK (groups::getTracks (*project, *drums) == juce::Array<te::AudioTrack*> { &lead, &bass });

        project->undo();
        CHECK (groups::getGroup (bass) == nullptr);
        project->undo();
        CHECK (project->getGroupTracks().size() == 1);
        project->undo();
        CHECK (project->getGroupTracks().isEmpty());
        CHECK (groups::getGroup (lead) == nullptr);

        project->redo();
        REQUIRE (project->getGroupTracks().size() == 1);
        CHECK (groups::getGroup (lead) == project->getGroupTracks().getFirst());

        groups::renameGroup (*project, *project->getGroupTracks().getFirst(), "  Drums ");
        CHECK (project->getGroupTracks().getFirst()->getName() == "Drums");
        project->undo();
        CHECK (project->getGroupTracks().getFirst()->getName() == "Group");
    }

    TEST_CASE ("a track keeps its group when tracks come, go or move before the group, and after saving and opening")
    {
        auto& session = test::session();
        juce::TemporaryFile file (".harmonia");

        {
            auto project = core::Project::createEmpty (session);
            auto& lead = *project->getTracks().getFirst();
            lead.setName ("Lead");
            auto& bass = *project->addTrack ("Bass");
            auto* group = groups::addGroup (*project, { &bass }, "Low end");
            REQUIRE (group != nullptr);
            const auto numberBefore = group->getAudioTrackNumber();

            // Tracktion keeps the group's number: two tracks above it make that number another track's.
            auto& edit = project->getEdit();
            REQUIRE (edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr) != nullptr);
            REQUIRE (edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr) != nullptr);
            CHECK (group->getAudioTrackNumber() == numberBefore + 2);
            CHECK (groups::getGroup (bass) == group);
            CHECK (groups::getGroup (lead) == nullptr);

            edit.deleteTrack (project->getTracks().getFirst());
            edit.moveTrack (&bass, te::TrackInsertPoint (nullptr, nullptr));
            CHECK (groups::getGroup (bass) == group);

            REQUIRE (project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = core::Project::load (session, file.getFile(), error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        REQUIRE (loaded->getGroupTracks().size() == 1);
        auto* group = loaded->getGroupTracks().getFirst();
        CHECK (group->getName() == "Low end");

        te::AudioTrack* bass = nullptr;

        for (auto* track : loaded->getTracks())
            if (track->getName() == "Bass")
                bass = track;

        REQUIRE (bass != nullptr);
        CHECK (groups::getGroup (*bass) == group);
        CHECK (groups::getTracks (*loaded, *group) == juce::Array<te::AudioTrack*> { bass });

        // Still after another track above, in the opened project.
        REQUIRE (loaded->getEdit().insertNewAudioTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr) != nullptr);
        CHECK (groups::getGroup (*bass) == group);
    }

    TEST_CASE ("deleting a group puts its tracks back on the master; undo brings both back")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& lead = *project->getTracks().getFirst();
        auto* group = groups::addGroup (*project, { &lead });
        REQUIRE (group != nullptr);
        project->clearUndoHistory();

        groups::removeGroup (*project, *group);
        CHECK (project->getGroupTracks().isEmpty());
        CHECK (groups::getGroup (lead) == nullptr);
        CHECK (lead.getOutput().getDestinationTrack() == nullptr);

        project->undo();
        REQUIRE (project->getGroupTracks().size() == 1);
        CHECK (groups::getGroup (lead) == project->getGroupTracks().getFirst());

        // Only groups: a song track isn't deleted this way.
        groups::removeGroup (*project, lead);
        CHECK (project->getTracks().size() == 1);
    }

    TEST_CASE ("a group's fader and mute act on its tracks in the export; a soloed track is heard through its group")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->getEdit().deleteTrack (project->getTracks().getFirst());
        auto& lead = addToneTrack (*project, temp, "Lead");

        const auto alone = steadyRms (exportMix (*project));
        REQUIRE (alone > 0.05f);

        auto* group = groups::addGroup (*project, { &lead });
        REQUIRE (group != nullptr);
        CHECK (steadyRms (exportMix (*project)) == doctest::Approx (alone).epsilon (0.02));

        group->getVolumePlugin()->setVolumeDb (-12.0f);
        CHECK (steadyRms (exportMix (*project)) == doctest::Approx (alone * minus12Db).epsilon (0.03));

        group->setMute (true);
        CHECK (lead.isMuted (true));
        {
            juce::TemporaryFile file (".wav");
            CHECK (audio::exportWav (project->getEdit(), file.getFile()).contains ("muted"));
        }
        group->setMute (false);

        // Another track, the lead soloed: only the lead, through its group.
        addToneTrack (*project, temp, "Pad").getVolumePlugin()->setVolumeDb (6.0f);
        lead.setSolo (true);
        CHECK (steadyRms (exportMix (*project)) == doctest::Approx (alone * minus12Db).epsilon (0.03));
    }

    TEST_CASE ("a grouped track's stem goes through its group; the track rendered alone doesn't, and stays in it")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->getEdit().deleteTrack (project->getTracks().getFirst());
        auto& lead = addToneTrack (*project, temp, "Lead");
        auto& pad = addToneTrack (*project, temp, "Pad");

        const auto alone = steadyRms (renderAlone (lead));
        REQUIRE (alone > 0.05f);

        auto* group = groups::addGroup (*project, { &lead }, "Drums");
        REQUIRE (group != nullptr);
        group->getVolumePlugin()->setVolumeDb (-12.0f);
        REQUIRE (project->save (temp.getFile ("song.harmonia")).isEmpty());

        CHECK (steadyRms (exportStem (*project, lead, temp)) == doctest::Approx (alone * minus12Db).epsilon (0.03));
        CHECK (steadyRms (exportStem (*project, pad, temp)) == doctest::Approx (alone).epsilon (0.03));

        CHECK (steadyRms (renderAlone (lead)) == doctest::Approx (alone).epsilon (0.02));
        CHECK (groups::getGroup (lead) == group);
        CHECK_FALSE (project->hasUnsavedChanges());
    }
}

TEST_SUITE ("audio::sidechain")
{
    TEST_CASE ("a compressor listening to the kick ducks the bass on each hit: in the mix, the bass's stem and the bass alone")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->getEdit().deleteTrack (project->getTracks().getFirst());
        auto& kick = addHitsTrack (*project, temp, "Kick");
        auto& bass = addToneTrack (*project, temp, "Bass");
        auto& compressor = addDucker (bass);
        project->clearUndoHistory();

        CHECK (sidechain::canListen (compressor));
        CHECK (sidechain::getSource (compressor) == nullptr);
        CHECK (sidechain::getPossibleSources (*project, compressor) == juce::Array<te::AudioTrack*> { &kick });

        // Listening to its own steady bass, it holds it down evenly.
        CHECK (renderAlone (bass).getDuckRatio() == doctest::Approx (1.0).epsilon (0.1));

        REQUIRE (sidechain::setSource (*project, compressor, &kick));
        CHECK (sidechain::getSource (compressor) == &kick);
        CHECK (compressor.useSidechainTrigger.get());
        CHECK (sidechain::getSourcesFor ({ &bass }) == juce::Array<te::AudioTrack*> { &kick });

        // The kick muted still ducks the bass, as while playing; the mix is then the bass alone.
        kick.setMute (true);
        const auto mix = exportMix (*project);
        CHECK (mix.getDuckRatio() < 0.5f);
        kick.setMute (false);

        // The stem: the bass ducking, without the kick's own sound.
        CHECK (exportStem (*project, bass, temp).getDuckRatio() < 0.5f);
        CHECK (renderAlone (bass).getDuckRatio() < 0.5f);
        CHECK_FALSE (kick.isMuted (false));

        project->undo();
        CHECK (sidechain::getSource (compressor) == nullptr);
        CHECK_FALSE (compressor.useSidechainTrigger.get());
        project->redo();
        CHECK (sidechain::getSource (compressor) == &kick);

        REQUIRE (sidechain::setSource (*project, compressor, nullptr));
        CHECK (sidechain::getSource (compressor) == nullptr);
        CHECK_FALSE (compressor.useSidechainTrigger.get());
    }

    TEST_CASE ("an effect can't listen to a track that hears it, and stops listening to a deleted track")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& kick = *project->getTracks().getFirst();
        auto& bass = *project->addTrack ("Bass");
        auto& pad = *project->addTrack ("Pad");
        auto& bassCompressor = addDucker (bass);
        auto& kickCompressor = addDucker (kick);

        REQUIRE (sidechain::setSource (*project, bassCompressor, &kick));

        // The kick listening to the bass would go round in a circle; so would the bass listening to itself.
        CHECK (sidechain::getPossibleSources (*project, kickCompressor) == juce::Array<te::AudioTrack*> { &pad });
        CHECK_FALSE (sidechain::setSource (*project, kickCompressor, &bass));
        CHECK_FALSE (sidechain::setSource (*project, bassCompressor, &bass));
        CHECK (sidechain::getSource (kickCompressor) == nullptr);

        // Nor round a longer circle: the pad listens to the bass, which listens to the kick.
        auto& padCompressor = addDucker (pad);
        REQUIRE (sidechain::setSource (*project, padCompressor, &bass));
        CHECK (sidechain::getPossibleSources (*project, kickCompressor).isEmpty());
        CHECK (sidechain::getSourcesFor ({ &pad }) == juce::Array<te::AudioTrack*> { &bass, &kick });

        // Effects on group and return tracks listen to song tracks (only those are offered).
        juce::String error;
        auto* reverb = audio::sends::addReturnTrack (*project, audio::sends::Preset::reverb, error);
        REQUIRE (reverb != nullptr);
        auto* group = groups::addGroup (*project, { &pad });
        REQUIRE (group != nullptr);
        const juce::Array<te::AudioTrack*> songTracks { &kick, &bass, &pad };
        CHECK (sidechain::getPossibleSources (*project, addDucker (*reverb)) == songTracks);
        CHECK (sidechain::getPossibleSources (*project, addDucker (*group)) == songTracks);

        // The master's effects hear everything already.
        CHECK (sidechain::getPossibleSources (*project, addCompressor (project->getEdit().getMasterPluginList())).isEmpty());

        project->beginGesture ("Delete the kick");
        sidechain::forgetSource (kick);
        project->getEdit().deleteTrack (&kick);
        CHECK (sidechain::getSource (bassCompressor) == nullptr);
        CHECK_FALSE (bassCompressor.useSidechainTrigger.get());
    }
}
