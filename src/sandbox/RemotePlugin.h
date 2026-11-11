#pragma once

#include "sandbox/Sandbox.h"

#include <map>
#include <optional>

namespace harmonia::sandbox
{
    class Channel;
    class RemoteParameter;
    namespace platform { class Process; class SharedMemory; class Signal; }
    namespace protocol { struct Block; }

    /** Stands in for a plugin that runs in a process of its own (a Host there): Tracktion and
        Harmonia use it as any loaded plugin — sound and MIDI, parameters, state, programs, bus
        layouts, latency. The sound goes through shared memory and the audio thread waits for each
        block only so long; if the process crashes or hangs, the plugin goes silent (an effect lets
        its input through) until reload(). */
    class RemotePlugin final : public juce::AudioPluginInstance,
                               private juce::Timer,
                               private juce::AsyncUpdater
    {
    public:
        ~RemotePlugin() override;

        /** True from a crash (or hang) of its process until it is reloaded. */
        bool hasCrashed() const noexcept    { return crashed.load(); }

        /** True if the crash was a hang (the process was stopped for not answering). */
        bool wasHung() const noexcept       { return hung.load(); }

        /** Starts it again in the state it was in before the crash (message thread): an error
            message, or empty. */
        juce::String reload();

        /** Shows its own window (its process does), at `topLeft` the first time; called again it
            comes to the front. False if it has none or it couldn't. */
        bool showEditor (juce::Point<int> topLeft, const juce::String& title, bool bringToFront);

        /** Closes its window: where it was (empty if it wasn't open). */
        juce::Rectangle<int> hideEditor();

        bool isEditorShowing() const noexcept    { return editorShowing; }

        /** Its window stays above every other window (now, if it is open, and when it is shown). */
        void setEditorAlwaysOnTop (bool);

        /** Its window is open and its process said it is above the others. */
        bool isEditorAlwaysOnTop() const noexcept    { return editorShowing && editorOnTop; }

        /** The window was closed from its own close button, or by a crash (message thread). */
        std::function<void (juce::Rectangle<int> lastBounds)> onEditorClosed;

        /** Blocks the plugin's process didn't finish in time (too slow, or hung). */
        int getNumMissedBlocks() const noexcept    { return missedBlocks.load(); }

        //==============================================================================
        const juce::String getName() const override    { return description.name; }
        void fillInPluginDescription (juce::PluginDescription& d) const override    { d = description; }

        void prepareToPlay (double sampleRate, int maximumBlockSize) override;
        void releaseResources() override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using juce::AudioPluginInstance::processBlock;
        using juce::AudioPluginInstance::processBlockBypassed;
        void reset() override;
        void setNonRealtime (bool) noexcept override;

        double getTailLengthSeconds() const override    { return tailSeconds; }
        bool acceptsMidi() const override               { return midiIn; }
        bool producesMidi() const override              { return midiOut; }
        bool isMidiEffect() const override              { return midiEffect; }
        bool supportsMPE() const override               { return mpe; }

        bool hasEditor() const override                 { return editorAvailable; }
        juce::AudioProcessorEditor* createEditor() override    { return nullptr; }

        int getNumPrograms() override                   { return programNames.size(); }
        int getCurrentProgram() override                { return currentProgram; }
        void setCurrentProgram (int) override;
        const juce::String getProgramName (int index) override    { return programNames[index]; }
        void changeProgramName (int index, const juce::String&) override;

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        void updateTrackProperties (const TrackProperties&) override;

    protected:
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processorLayoutsChanged() override;

    private:
        friend class Sandbox;
        friend class Sandbox::Shared;
        friend class RemoteParameter;

        /** The memory and signals the sound goes through: the same for every process it runs in. */
        struct Link;

        /** One run of its process. */
        struct Connection
        {
            std::unique_ptr<platform::Process> process;
            std::shared_ptr<Channel> channel;
        };

        RemotePlugin (std::shared_ptr<Sandbox::Shared>, std::unique_ptr<Link>, Connection, const juce::ValueTree& info);

        static BusesProperties makeBuses (const juce::ValueTree& info);

        static std::unique_ptr<RemotePlugin> load (std::shared_ptr<Sandbox::Shared>, const juce::PluginDescription&,
                                                   double sampleRate, int blockSize, juce::String& error);
        static juce::ValueTree start (Sandbox::Shared&, Link&, Connection&, const juce::PluginDescription&,
                                      double sampleRate, int blockSize, juce::String& error);

        std::shared_ptr<Sandbox::Shared> shared;
        std::unique_ptr<Link> link;
        protocol::Block& block;

        mutable juce::CriticalSection connectionLock;
        Connection connection;

        juce::PluginDescription description;
        juce::Array<RemoteParameter*> remoteParameters;
        bool editorAvailable = false, midiIn = false, midiOut = false, midiEffect = false, mpe = false;
        double tailSeconds = 0;
        juce::StringArray programNames;
        int currentProgram = 0;

        std::atomic<bool> crashed { false }, hung { false }, reloading { false }, resetPending { false };
        std::atomic<int> activeProcessCalls { 0 }, missedBlocks { 0 };
        std::atomic<double> requestStartMs { 0 };
        std::atomic<bool> requestIsOffline { false };
        juce::MidiBuffer midiOutput;

        bool prepared = false;
        double preparedRate = 0;
        int preparedBlockSize = 0;
        bool editorShowing = false;
        bool editorWantedOnTop = false, editorOnTop = false;
        std::optional<TrackProperties> trackProperties;

        juce::CriticalSection stateLock;
        juce::MemoryBlock lastState;

        mutable juce::CriticalSection cacheLock;
        mutable std::map<std::pair<int, float>, juce::String> textCache;
        mutable std::map<juce::String, bool> layoutCache;
        mutable double textUnavailableUntilMs = 0;

        juce::CriticalSection incomingLock;
        std::vector<juce::ValueTree> incoming;

        std::shared_ptr<Channel> getChannel() const;
        juce::ValueTree call (juce::ValueTree request, int timeoutMs) const;
        void attach (Connection);
        void markCrashed();
        void receive (const juce::ValueTree&);
        void handleAsyncUpdate() override;
        void timerCallback() override;
        void handleNote (const juce::ValueTree&);
        void applyLatency (const juce::ValueTree& reply);

        void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
        bool processPart (juce::AudioBuffer<float>&, int numChannels, int start, int numSamples,
                          const juce::MidiBuffer& midi, const juce::Optional<juce::AudioPlayHead::PositionInfo>&, bool bypassed);
        void stayQuiet (juce::AudioBuffer<float>&, int fromSample);

        /** The watchdog: stops the process if it has been on one block too long (any thread). */
        void checkForHang (double nowMs);

        juce::String getParameterText (int index, float value) const;
        float getParameterValueForText (int index, const juce::String&) const;
        void parameterSet (int index, float value) noexcept;

        JUCE_DECLARE_NON_COPYABLE (RemotePlugin)
    };
}
