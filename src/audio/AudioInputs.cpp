#include "audio/AudioInputs.h"
#include "core/Tracks.h"

namespace harmonia::audio
{
    namespace
    {
        const juce::Identifier armedId ("hRecordArmed");
        const juce::Identifier inputId ("hRecordInput");
    }

    AudioInputs::AudioInputs (te::Edit& e)
        : edit (e)
    {
        edit.engine.getDeviceManager().addChangeListener (this);
        edit.state.addListener (this);
        refresh();
    }

    AudioInputs::~AudioInputs()
    {
        edit.state.removeListener (this);
        edit.engine.getDeviceManager().removeChangeListener (this);
        cancelPendingUpdate();
    }

    //==============================================================================
    std::vector<te::WaveInputDevice*> AudioInputs::getInputs (te::Engine& engine)
    {
        std::vector<te::WaveInputDevice*> inputs;

        for (auto* device : engine.getDeviceManager().getWaveInputDevices())
            if (device != nullptr && device->isEnabled() && device->getDeviceType() == te::InputDevice::waveDevice)
                inputs.push_back (device);

        return inputs;
    }

    te::WaveInputDevice* AudioInputs::getInput (const te::AudioTrack& track)
    {
        const auto inputs = getInputs (track.edit.engine);

        if (inputs.empty())
            return nullptr;

        const auto chosen = track.state[inputId].toString();

        for (auto* input : inputs)
            if (input->getName() == chosen)
                return input;

        return inputs.front();
    }

    void AudioInputs::setInput (te::AudioTrack& track, const juce::String& inputName)
    {
        // A setting of the track, like its colour would be: not an undo step.
        track.state.setProperty (inputId, inputName, nullptr);
    }

    bool AudioInputs::isArmed (const te::AudioTrack& track)
    {
        return core::tracks::isAudio (track) && (bool) track.state.getProperty (armedId, false);
    }

    void AudioInputs::setArmed (te::AudioTrack& track, bool shouldBeArmed)
    {
        if (shouldBeArmed && core::tracks::isAudio (track))
            track.state.setProperty (armedId, true, nullptr);
        else
            track.state.removeProperty (armedId, nullptr);
    }

    bool AudioInputs::isMonitoring (te::Engine& engine)
    {
        for (auto* input : getInputs (engine))
            if (input->getMonitorMode() == te::InputDevice::MonitorMode::off)
                return false;

        return true;
    }

    void AudioInputs::setMonitoring (te::Engine& engine, bool shouldMonitor)
    {
        for (auto* input : getInputs (engine))
            input->setMonitorMode (shouldMonitor ? te::InputDevice::MonitorMode::automatic
                                                 : te::InputDevice::MonitorMode::off);
    }

    bool AudioInputs::anyArmed() const
    {
        for (auto* track : te::getAudioTracks (edit))
            if (isArmed (*track) && getInput (*track) != nullptr)
                return true;

        return false;
    }

    //==============================================================================
    void AudioInputs::refresh()
    {
        auto& transport = edit.getTransport();

        // Assignments can't change under a recording; Stop brings them up to date.
        if (transport.isRecording())
            return;

        transport.ensureContextAllocated();
        const auto tracks = te::getAudioTracks (edit);

        for (auto* instance : edit.getAllInputDevices())
        {
            auto& device = instance->owner;

            if (device.getDeviceType() != te::InputDevice::waveDevice)
                continue;

            juce::Array<te::EditItemID> wanted;

            for (auto* track : tracks)
                if (isArmed (*track) && getInput (*track) == &device)
                    wanted.add (track->itemID);

            for (auto id : instance->getTargets())
                if (! wanted.contains (id))
                    [[maybe_unused]] auto result = instance->removeTarget (id, nullptr);

            for (auto id : wanted)
            {
                if (! instance->getTargets().contains (id))
                    if (! instance->setTarget (id, false, nullptr).has_value())
                        continue;

                if (! instance->isRecordingEnabled (id))
                    instance->setRecordingEnabled (id, true);
            }
        }
    }

    void AudioInputs::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // The engine rebuilds its inputs first (devices restarted or changed); assign them afterwards.
        juce::MessageManager::callAsync ([ref = juce::WeakReference<AudioInputs> (this)]
        {
            if (ref != nullptr)
                ref->refresh();
        });
    }

    void AudioInputs::handleAsyncUpdate()
    {
        refresh();
    }

    void AudioInputs::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
    {
        if (tree.hasType (te::IDs::TRACK) && (id == armedId || id == inputId || id == juce::Identifier ("hTrackKind")))
            triggerAsyncUpdate();
    }

    void AudioInputs::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
    {
        if (child.hasType (te::IDs::TRACK))
            triggerAsyncUpdate();
    }

    void AudioInputs::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
    {
        if (child.hasType (te::IDs::TRACK))
            triggerAsyncUpdate();
    }
}
