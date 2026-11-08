#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/AudioInputs.h"
#include "audio/Instruments.h"
#include "audio/Transport.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using audio::AudioInputs;
namespace clips = core::clips;
namespace tracks = core::tracks;

namespace
{
    /** An engine of its own (so the shared test engine never gets a device), whose "audio
        interface" is fed by the test: what `process` is given arrives at its inputs. */
    struct RecordingRig
    {
        explicit RecordingRig (int numInputs = 1)
        {
            params.sampleRate = 44100.0;
            params.blockSize = 512;
            params.inputChannels = numInputs;
            params.outputChannels = 2;
            audioIO.initialise (params);
            audioIO.prepareToPlay (params.sampleRate, params.blockSize);
            engine().getDeviceManager().dispatchPendingUpdates();
        }

        ~RecordingRig()
        {
            project.reset();
            engine().getDeviceManager().deviceManager.closeAudioDevice();
            engine().getDeviceManager().removeHostedAudioDeviceInterface();
            engine().getAudioFileManager().releaseAllFiles();
        }

        te::Engine& engine()    { return session.getEngine(); }

        static core::Session::Options makeOptions (const juce::File& recordings)
        {
            core::Session::Options options;
            options.openDevices = false;
            options.settingsName = "HarmoniaRecordingTests";
            options.recordingsFolder = recordings;
            return options;
        }

        /** Runs the engine for `seconds` with a tone (or silence) at the inputs. */
        void process (double seconds, float level = 0.5f)
        {
            const auto numSamples = (int) (seconds * params.sampleRate);
            const auto numChannels = std::max (params.inputChannels, params.outputChannels);
            juce::MidiBuffer midi;

            for (int start = 0; start < numSamples; start += params.blockSize)
            {
                const auto n = std::min (params.blockSize, numSamples - start);
                juce::AudioBuffer<float> block (numChannels, n);
                block.clear();

                for (int channel = 0; channel < params.inputChannels; ++channel)
                    for (int i = 0; i < n; ++i)
                        block.setSample (channel, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0
                                                                               * (samplesProcessed + i) / params.sampleRate));

                audioIO.processBlock (block, midi);
                samplesProcessed += n;
            }
        }

        te::AudioTrack& addAudioTrack (const juce::String& name)
        {
            auto* track = project->addTrack (name);
            tracks::setKind (*track, tracks::Kind::audio);
            return *track;
        }

        test::TempFolder temp;
        core::Session session { makeOptions (temp.getFile ("Recordings")) };
        te::HostedAudioDeviceInterface::Parameters params;
        te::HostedAudioDeviceInterface& audioIO { engine().getDeviceManager().getHostedAudioDeviceInterface() };
        std::unique_ptr<core::Project> project = core::Project::createEmpty (session);
        juce::int64 samplesProcessed = 0;
    };

    float rmsOf (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
            return -1.0f;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        return buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    }
}

TEST_SUITE ("audio::recording")
{
    TEST_CASE ("only audio tracks can be armed; the arm and the input are saved with the project")
    {
        RecordingRig rig;
        auto& instrumentTrack = *rig.project->getTracks().getFirst();
        auto& audioTrack = rig.addAudioTrack ("Vocals");

        AudioInputs::setArmed (instrumentTrack, true);
        CHECK_FALSE (AudioInputs::isArmed (instrumentTrack));

        const auto inputs = AudioInputs::getInputs (rig.engine());
        REQUIRE (! inputs.empty());
        CHECK (AudioInputs::getInput (audioTrack) == inputs.front());

        AudioInputs::setArmed (audioTrack, true);
        AudioInputs::setInput (audioTrack, inputs.front()->getName());
        CHECK (AudioInputs::isArmed (audioTrack));

        // A track asked for an input that isn't there any more records from the first one.
        AudioInputs::setInput (audioTrack, "Gone");
        CHECK (AudioInputs::getInput (audioTrack) == inputs.front());
        AudioInputs::setInput (audioTrack, inputs.front()->getName());

        const auto file = rig.temp.getFile ("Song.harmonia");
        REQUIRE (rig.project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (rig.session, file, error);
        REQUIRE (loaded != nullptr);
        CHECK (AudioInputs::isArmed (*loaded->getTracks()[1]));
        CHECK_FALSE (AudioInputs::isArmed (*loaded->getTracks()[0]));
        CHECK (AudioInputs::getInput (*loaded->getTracks()[1]) == inputs.front());
    }

    TEST_CASE ("armed audio tracks get their input in the engine, the others don't")
    {
        RecordingRig rig (2);
        auto& armed = rig.addAudioTrack ("Armed");
        auto& other = rig.addAudioTrack ("Other");
        audio::Transport transport (rig.project->getEdit());
        AudioInputs inputs (rig.project->getEdit());

        CHECK_FALSE (inputs.anyArmed());
        AudioInputs::setArmed (armed, true);
        inputs.refresh();
        CHECK (inputs.anyArmed());

        const auto targetsOf = [&] (const te::AudioTrack& track)
        {
            int count = 0;

            for (auto* instance : rig.project->getEdit().getAllInputDevices())
                if (instance->owner.getDeviceType() == te::InputDevice::waveDevice
                    && instance->getTargets().contains (track.itemID) && instance->isRecordingEnabled (track.itemID))
                    ++count;

            return count;
        };

        CHECK (targetsOf (armed) == 1);
        CHECK (targetsOf (other) == 0);

        AudioInputs::setArmed (armed, false);
        inputs.refresh();
        CHECK (targetsOf (armed) == 0);
        CHECK_FALSE (inputs.anyArmed());
    }

    TEST_CASE ("an armed audio track records its input into a new clip, as one undo step")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Vocals");
        auto& idle = rig.addAudioTrack ("Idle");
        auto& edit = rig.project->getEdit();
        audio::Transport transport (edit);
        AudioInputs inputs (edit);

        AudioInputs::setArmed (track, true);
        inputs.refresh();
        edit.dispatchPendingUpdatesSynchronously();
        rig.project->clearUndoHistory();

        transport.recordsAudio = [&inputs] { return inputs.anyArmed(); };
        transport.setLoopBeats (0.0, 4.0);
        transport.setPositionSeconds (1.0);
        transport.play();
        CHECK (transport.isRecordRequested());
        CHECK (transport.isRecording());
        CHECK_FALSE (edit.getTransport().looping.get());   // straight on, no loop while recording
        CHECK (transport.getRecordStartSeconds() == doctest::Approx (1.0));

        rig.process (2.0);
        transport.stop();
        CHECK_FALSE (transport.isRecording());
        CHECK (edit.getTransport().looping.get());          // the loop is back

        const auto recorded = clips::getClips (track);
        REQUIRE (recorded.size() == 1);
        CHECK (clips::getClips (idle).isEmpty());
        CHECK (clips::isAudio (*recorded[0]));
        CHECK (clips::getStartBeat (*recorded[0]) == doctest::Approx (2.0).epsilon (0.02));   // 1 s at 120 BPM
        CHECK (clips::getEndBeat (*recorded[0]) - clips::getStartBeat (*recorded[0]) == doctest::Approx (4.0).epsilon (0.05));

        const auto file = dynamic_cast<te::AudioClipBase*> (recorded[0])->getCurrentSourceFile();
        CHECK (file.existsAsFile());
        CHECK (file.isAChildOf (rig.temp.getFile ("Recordings")));
        CHECK (file.getFileName().startsWith ("Vocals "));
        CHECK (rmsOf (file) > 0.2f);

        CHECK (edit.getUndoManager().getUndoDescription() == "Record audio");
        rig.project->undo();
        CHECK (clips::getClips (track).isEmpty());
        rig.project->redo();
        CHECK (clips::getClips (track).size() == 1);
    }

    TEST_CASE ("without armed tracks Play just plays")
    {
        RecordingRig rig;
        rig.addAudioTrack ("Vocals");
        auto& edit = rig.project->getEdit();
        audio::Transport transport (edit);
        AudioInputs inputs (edit);

        transport.recordsAudio = [&inputs] { return inputs.anyArmed(); };
        transport.setLoopBeats (0.0, 4.0);
        transport.play();
        CHECK_FALSE (transport.isRecordRequested());
        CHECK_FALSE (transport.isRecording());
        CHECK (transport.isPlaying());
        CHECK (edit.getTransport().looping.get());
        rig.process (0.5);
        transport.stop();
    }

    TEST_CASE ("recordings go next to a saved project, or to the recordings folder")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Lead: vox?");
        const auto folder = rig.temp.getFile ("Recordings");

        auto file = core::Session::getFileForNewRecording (track, "wav", folder);
        CHECK (file.getParentDirectory() == folder);
        CHECK (file.hasFileExtension ("wav"));
        CHECK (file.getFileName().startsWith ("Lead vox "));

        REQUIRE (rig.project->save (rig.temp.getFile ("My Song.harmonia")).isEmpty());
        file = core::Session::getFileForNewRecording (track, ".wav", folder);
        CHECK (file.getParentDirectory() == rig.temp.getFile ("My Song Audio"));
    }

    TEST_CASE ("the arrangement shows a recording in progress and the arm buttons of audio tracks")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Vocals");
        auto main = std::make_unique<ui::MainComponent> (rig.session, *rig.project);
        main->setBounds (0, 0, 1280, 800);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        auto& headers = main->getArrangement().getHeaders();
        REQUIRE (headers.getNumHeaders() == 2);
        CHECK (headers.getArmButton (0) == nullptr);
        auto* arm = headers.getArmButton (1);
        REQUIRE (arm != nullptr);

        arm->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK (AudioInputs::isArmed (track));
        CHECK (main->getAudioInputs().anyArmed());

        auto& clipArea = main->getArrangement().getClipArea();
        clipArea.setRecording (ui::arrangement::ClipArea::RecordingMarker { 2.0, 9.0, { 1 } });
        CHECK (main->createComponentSnapshot (main->getLocalBounds()).isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-audio-recording.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (main->createComponentSnapshot (main->getLocalBounds()), out);
        }

        main.reset();
    }
}
