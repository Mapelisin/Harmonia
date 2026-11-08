#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Recording sound from the audio interface's inputs (a microphone, a guitar...) onto audio
        tracks.

        Each audio track remembers which input it records from and whether it is armed (both
        saved with the project). This keeps the engine's input assignments in line with that:
        every armed audio track gets its input, so when the transport records (audio::Transport
        with recordsAudio) the engine writes a file per armed track and makes it a clip at Stop.
        An armed track also lets the input be heard (monitoring), unless that is turned off. */
    class AudioInputs final : private juce::ChangeListener,
                              private juce::ValueTree::Listener,
                              private juce::AsyncUpdater
    {
    public:
        explicit AudioInputs (te::Edit&);
        ~AudioInputs() override;

        /** The audio interface's inputs (mono channels or stereo pairs) that are turned on. */
        static std::vector<te::WaveInputDevice*> getInputs (te::Engine&);

        /** The input a track records from: the one chosen for it if it is still there, otherwise
            the first input. nullptr if the audio interface has no inputs. */
        static te::WaveInputDevice* getInput (const te::AudioTrack&);
        static void setInput (te::AudioTrack&, const juce::String& inputName);

        /** Armed: records its input when the transport records. Only audio tracks can be armed. */
        static bool isArmed (const te::AudioTrack&);
        static void setArmed (te::AudioTrack&, bool);

        /** Whether armed tracks let their input be heard (for every input). On by default; off
            avoids the echo of hearing yourself late, or feedback with speakers. */
        static bool isMonitoring (te::Engine&);
        static void setMonitoring (te::Engine&, bool);

        /** Some audio track is armed and has an input to record from. */
        bool anyArmed() const;

        /** Brings the engine's input assignments in line with the tracks now (it also happens by
            itself shortly after any change). */
        void refresh();

    private:
        te::Edit& edit;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void handleAsyncUpdate() override;

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

        JUCE_DECLARE_WEAK_REFERENCEABLE (AudioInputs)
        JUCE_DECLARE_NON_COPYABLE (AudioInputs)
    };
}
