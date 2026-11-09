#include <doctest.h>

#include "TestAudioRig.h"
#include "audio/AudioInputs.h"
#include "audio/Instruments.h"
#include "audio/Transport.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Takes.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using audio::AudioInputs;
namespace clips = core::clips;
namespace tracks = core::tracks;

namespace
{
    using RecordingRig = test::AudioRig;

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

    TEST_CASE ("a count-in clicks before the recording, which still starts where Play was pressed")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Vocals");
        auto& edit = rig.project->getEdit();
        audio::Transport transport (edit);
        AudioInputs inputs (edit);

        // The count-in is a setting of the app: put it back whatever happens.
        struct Restore
        {
            audio::Transport& t;
            ~Restore()  { t.setCountIn (audio::Transport::CountIn::off); }
        } restore { transport };

        transport.setCountIn (audio::Transport::CountIn::oneBar);
        CHECK (transport.getCountIn() == audio::Transport::CountIn::oneBar);
        AudioInputs::setArmed (track, true);
        inputs.refresh();
        edit.dispatchPendingUpdatesSynchronously();

        transport.recordsAudio = [&inputs] { return inputs.anyArmed(); };
        transport.setPositionSeconds (4.0);   // bar 3
        transport.play();
        CHECK (transport.isRecording());
        CHECK (transport.isCountingIn());
        CHECK (transport.getPositionSeconds() < 2.5);   // a bar (2 s at 120 BPM) and a bit earlier

        // Only clicks come out while counting in (the input is silent).
        const auto clicks = rig.process (1.5, 0.0f);
        CHECK (transport.isCountingIn());
        CHECK (clicks > 0.001f);

        rig.process (1.0);   // past the start: recording the tone
        CHECK_FALSE (transport.isCountingIn());
        rig.process (1.5);
        transport.stop();

        const auto recorded = clips::getClips (track);
        REQUIRE (recorded.size() == 1);
        CHECK (clips::getStartBeat (*recorded[0]) == doctest::Approx (8.0).epsilon (0.02));
        CHECK (clips::getEndBeat (*recorded[0]) - clips::getStartBeat (*recorded[0]) < 4.5);   // not the count-in
        CHECK (rmsOf (dynamic_cast<te::AudioClipBase*> (recorded[0])->getCurrentSourceFile()) > 0.2f);

        transport.setCountIn (audio::Transport::CountIn::off);
        CHECK (edit.getNumCountInBeats() == 0);
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

TEST_SUITE ("audio::takes")
{
    namespace takes = core::takes;

    /** Records round beats 4..8 (2 s at 120 BPM) for `seconds`; returns the clip made. */
    te::AudioClipBase* recordTakes (RecordingRig& rig, audio::Transport& transport, AudioInputs& inputs, te::AudioTrack& track, double seconds)
    {
        auto& edit = rig.project->getEdit();
        AudioInputs::setArmed (track, true);
        inputs.refresh();
        edit.dispatchPendingUpdatesSynchronously();
        rig.project->clearUndoHistory();

        transport.recordsAudio = [&inputs] { return inputs.anyArmed(); };
        transport.setRecordsTakes (true);
        transport.setLoopBeats (4.0, 8.0);
        transport.setPositionSeconds (0.5);
        transport.play();
        CHECK (transport.isRecordingTakes());
        CHECK (edit.getTransport().looping.get());
        CHECK (transport.getRecordStartSeconds() == doctest::Approx (2.0));     // from the loop's start

        rig.process (seconds);
        transport.stop();
        CHECK_FALSE (transport.isRecordingTakes());
        CHECK (transport.getTakeNumber() == 0);

        const auto recorded = clips::getClips (track);
        REQUIRE (recorded.size() == 1);
        return dynamic_cast<te::AudioClipBase*> (recorded[0]);
    }

    TEST_CASE ("recording round a loop makes one clip with a take per time round; it plays the last full one")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Vocals");
        auto& edit = rig.project->getEdit();
        audio::Transport transport (edit);
        AudioInputs inputs (edit);

        auto* clip = recordTakes (rig, transport, inputs, track, 5.0);   // two times round and a half
        REQUIRE (clip != nullptr);
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (4.0).epsilon (0.02));
        CHECK (clips::getEndBeat (*clip) == doctest::Approx (8.0).epsilon (0.02));

        REQUIRE (takes::getCount (*clip) == 3);
        CHECK (takes::getCurrent (*clip) == 1);
        CHECK_FALSE (clip->state.getChildWithName (te::IDs::TAKES).isValid());   // not Tracktion's takes
        CHECK (takes::getName (1) == "Take 2");

        for (int i = 0; i < 3; ++i)
        {
            const auto file = takes::getFile (*clip, i);
            CHECK (file.existsAsFile());
            CHECK (file.isAChildOf (rig.temp.getFile ("Recordings")));
            CHECK (rmsOf (file) > 0.2f);
            CHECK (te::AudioFile (rig.engine(), file).getLength() == doctest::Approx (i < 2 ? 2.0 : 1.0).epsilon (0.1));
        }

        // The whole recording, takes and all, is one undo step.
        CHECK (edit.getUndoManager().getUndoDescription() == "Record audio");
        rig.project->undo();
        CHECK (clips::getClips (track).isEmpty());
        rig.project->redo();
        REQUIRE (clips::getClips (track).size() == 1);
        CHECK (takes::getCount (*clips::getClips (track)[0]) == 3);
    }

    TEST_CASE ("a take is chosen with undo, plays, and is saved with the song; split pieces choose their own")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Vocals");
        auto& edit = rig.project->getEdit();
        audio::Transport transport (edit);
        AudioInputs inputs (edit);

        auto* clip = recordTakes (rig, transport, inputs, track, 6.5);   // three full takes and a bit
        REQUIRE (clip != nullptr);
        REQUIRE (takes::getCount (*clip) == 4);
        CHECK (takes::getCurrent (*clip) == 2);
        AudioInputs::setArmed (track, false);
        inputs.refresh();
        edit.dispatchPendingUpdatesSynchronously();

        edit.getUndoManager().beginNewTransaction ("Choose take");
        CHECK (takes::choose (*clip, 0));
        CHECK (takes::getCurrent (*clip) == 0);
        CHECK (clip->getSourceFileReference().getFile() == takes::getFile (*clip, 0));
        CHECK_FALSE (takes::choose (*clip, 7));

        // It plays the take.
        edit.dispatchPendingUpdatesSynchronously();
        transport.recordsAudio = nullptr;
        transport.setLoopBeats (0.0, 0.0);
        transport.setPositionSeconds (2.0);
        transport.play();
        rig.process (0.2, 0.0f);
        CHECK (rig.process (1.0, 0.0f) > 0.1f);
        transport.stop();

        rig.project->undo();
        CHECK (takes::getCurrent (*clip) == 2);
        rig.project->redo();
        CHECK (takes::getCurrent (*clip) == 0);

        // Split: both pieces have the takes, each its own choice.
        edit.getUndoManager().beginNewTransaction ("Split");
        auto* right = dynamic_cast<te::AudioClipBase*> (clips::split (*clip, 6.0));
        REQUIRE (right != nullptr);
        CHECK (takes::getCount (*right) == 4);
        CHECK (takes::getCurrent (*right) == 0);
        CHECK (takes::choose (*right, 1));
        CHECK (takes::getCurrent (*clip) == 0);
        CHECK (takes::getCurrent (*right) == 1);

        // Saved and opened again.
        const auto file = rig.temp.getFile ("Song.harmonia");
        REQUIRE (rig.project->save (file).isEmpty());
        juce::String error;
        auto loaded = core::Project::load (rig.session, file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        te::AudioTrack* loadedTrack = nullptr;

        for (auto* t : loaded->getTracks())
            if (t->getName() == "Vocals")
                loadedTrack = t;

        REQUIRE (loadedTrack != nullptr);
        const auto loadedClips = clips::getClips (*loadedTrack);
        REQUIRE (loadedClips.size() == 2);
        CHECK (takes::getCount (*loadedClips[0]) == 4);
        CHECK (takes::getCurrent (*loadedClips[0]) == 0);
        CHECK (takes::getCurrent (*loadedClips[1]) == 1);
        loaded.reset();

        // Only the one it plays kept.
        edit.getUndoManager().beginNewTransaction ("Keep take");
        takes::keepOnlyCurrent (*right);
        CHECK (takes::getCount (*right) == 0);
        CHECK (takes::getCurrent (*right) == -1);
        CHECK (right->getSourceFileReference().getFile().existsAsFile());
        rig.project->undo();
        CHECK (takes::getCount (*right) == 4);
    }

    TEST_CASE ("takes after a count-in: the clicks play before the loop, the takes start at its start")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Vocals");
        auto& edit = rig.project->getEdit();
        audio::Transport transport (edit);
        AudioInputs inputs (edit);
        AudioInputs::setArmed (track, true);
        inputs.refresh();
        edit.dispatchPendingUpdatesSynchronously();

        transport.setCountIn (audio::Transport::CountIn::oneBar);
        transport.recordsAudio = [&inputs] { return inputs.anyArmed(); };
        transport.setRecordsTakes (true);
        transport.setLoopBeats (4.0, 8.0);
        transport.play();
        CHECK (transport.isCountingIn());
        CHECK (transport.getTakeNumber() == 0);
        rig.process (2.6);                  // the bar of clicks (2 s, starting a bit early) and a bit
        CHECK_FALSE (transport.isCountingIn());
        CHECK (transport.getTakeNumber() == 1);
        rig.process (2.0);
        CHECK (transport.getTakeNumber() == 2);
        rig.process (1.9);                  // 6.5 s: the count-in, two takes and some of a third
        transport.stop();
        transport.setCountIn (audio::Transport::CountIn::off);

        const auto recorded = clips::getClips (track);
        REQUIRE (recorded.size() == 1);
        CHECK (clips::getStartBeat (*recorded[0]) == doctest::Approx (4.0).epsilon (0.02));
        CHECK (clips::getEndBeat (*recorded[0]) == doctest::Approx (8.0).epsilon (0.02));
        CHECK (takes::getCount (*recorded[0]) == 3);
        CHECK (takes::getCurrent (*recorded[0]) == 1);
    }

    TEST_CASE ("without a loop, or with takes off, a recording goes straight on and has no takes")
    {
        RecordingRig rig;
        auto& track = rig.addAudioTrack ("Vocals");
        auto& edit = rig.project->getEdit();
        audio::Transport transport (edit);
        AudioInputs inputs (edit);
        AudioInputs::setArmed (track, true);
        inputs.refresh();
        edit.dispatchPendingUpdatesSynchronously();

        transport.recordsAudio = [&inputs] { return inputs.anyArmed(); };
        transport.setRecordsTakes (true);
        transport.setPositionSeconds (1.0);
        transport.play();                       // no loop
        CHECK_FALSE (transport.isRecordingTakes());
        rig.process (1.5);
        transport.stop();
        REQUIRE (clips::getClips (track).size() == 1);
        CHECK (takes::getCount (*clips::getClips (track)[0]) == 0);

        // One time round isn't worth a list either.
        rig.project->undo();
        transport.setLoopBeats (4.0, 8.0);
        transport.play();
        CHECK (transport.isRecordingTakes());
        rig.process (1.0);
        transport.stop();
        REQUIRE (clips::getClips (track).size() == 1);
        CHECK (takes::getCount (*clips::getClips (track)[0]) == 0);
    }
}
