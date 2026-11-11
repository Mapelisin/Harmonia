#include "sandbox/RemotePlugin.h"
#include "sandbox/Channel.h"
#include "sandbox/Protocol.h"
#include "sandbox/SandboxShared.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace harmonia::sandbox
{
    namespace
    {
        namespace message = protocol::message;
        namespace property = protocol::property;

        constexpr int connectTimeoutMs = 20000;
        constexpr int loadTimeoutMs    = 120000;   // a plugin may ask for its licence first
        constexpr int stateTimeoutMs   = 20000;
        constexpr int setupTimeoutMs   = 20000;    // prepare, release, layout
        constexpr int askTimeoutMs     = 5000;     // layouts, programs, windows
        constexpr int textTimeoutMs    = 500;      // a value as text, for a knob or a list

        double now()    { return juce::Time::getMillisecondCounterHiRes(); }

        /** The channels its buses use now, the more of inputs and outputs. */
        int countEnabledChannels (const juce::ValueTree& info)
        {
            int inputs = 0, outputs = 0;

            for (const auto& bus : info.getChildWithName (property::buses))
                if (bus[property::enabled])
                    (bus[property::input] ? inputs : outputs) += protocol::channelsFromString (bus[property::channels].toString()).size();

            return juce::jmax (inputs, outputs);
        }

        int countChannels (const juce::Array<juce::AudioChannelSet>& buses)
        {
            int total = 0;

            for (const auto& set : buses)
                total += set.size();

            return total;
        }

        juce::StringArray readPrograms (const juce::ValueTree& programs)
        {
            juce::StringArray names;

            for (const auto& program : programs)
                names.add (program[property::name].toString());

            return names;
        }

        juce::String toXml (const juce::PluginDescription& description)
        {
            if (auto xml = description.createXml())
                return xml->toString (juce::XmlElement::TextFormat().singleLine());

            return {};
        }

        juce::Rectangle<int> readBounds (const juce::ValueTree& tree)
        {
            return { tree[property::x], tree[property::y], tree[property::width], tree[property::height] };
        }

        /** The position `samples` later. */
        juce::AudioPlayHead::PositionInfo advance (juce::AudioPlayHead::PositionInfo position, int samples, double sampleRate)
        {
            if (samples <= 0 || sampleRate <= 0)
                return position;

            const auto seconds = samples / sampleRate;

            if (auto time = position.getTimeInSamples())
                position.setTimeInSamples (*time + samples);

            if (auto time = position.getTimeInSeconds())
                position.setTimeInSeconds (*time + seconds);

            if (auto ppq = position.getPpqPosition())
                if (auto bpm = position.getBpm())
                    position.setPpqPosition (*ppq + seconds * *bpm / 60.0);

            return position;
        }

        /** Counts the audio threads inside processBlock, so a reload knows when none is. */
        struct ActiveCall
        {
            explicit ActiveCall (std::atomic<int>& c) : count (c)    { ++count; }
            ~ActiveCall()                                            { --count; }

            std::atomic<int>& count;
        };
    }

    //==============================================================================
    /** A parameter of the plugin: its value is kept here (the plugin's process gets each change). */
    class RemoteParameter final : public juce::AudioPluginInstance::HostedParameter
    {
    public:
        RemoteParameter (RemotePlugin& p, int i, const juce::ValueTree& info)
            : owner (p), index (i),
              name (info[property::name].toString()), label (info[property::label].toString()),
              parameterId (info[property::parameterId].toString()),
              defaultValue (info[property::defaultValue]), steps (info[property::steps]),
              discrete (info[property::discrete]), boolean (info[property::boolean]),
              automatable (info[property::automatable]), meta (info[property::meta]),
              inverted (info[property::inverted]),
              category ((Category) (int) info[property::category]),
              valueStrings (juce::StringArray::fromLines (info[property::valueStrings].toString())),
              value ((float) info[property::value])
        {
            valueStrings.removeEmptyStrings (false);
        }

        float getValue() const override    { return value.load (std::memory_order_relaxed); }

        void setValue (float newValue) override
        {
            value.store (newValue, std::memory_order_relaxed);
            owner.parameterSet (index, newValue);
        }

        /** The plugin changed it itself (or in its window). */
        void setValueFromPlugin (float newValue)
        {
            if (value.exchange (newValue) != newValue)
                sendValueChangedMessageToListeners (newValue);
        }

        void rename (const juce::String& newName, const juce::String& newLabel)
        {
            name = newName;
            label = newLabel;
        }

        float getDefaultValue() const override                     { return defaultValue; }
        juce::String getName (int maximumLength) const override    { return name.substring (0, maximumLength); }
        juce::String getLabel() const override                     { return label; }
        int getNumSteps() const override                           { return steps; }
        bool isDiscrete() const override                           { return discrete; }
        bool isBoolean() const override                            { return boolean; }
        bool isAutomatable() const override                        { return automatable; }
        bool isMetaParameter() const override                      { return meta; }
        bool isOrientationInverted() const override                { return inverted; }
        Category getCategory() const override                      { return category; }
        juce::StringArray getAllValueStrings() const override      { return valueStrings; }
        juce::String getParameterID() const override               { return parameterId; }

        juce::String getText (float normalisedValue, int maximumLength) const override
        {
            if (valueStrings.size() > 1)
                return valueStrings[juce::roundToInt (normalisedValue * (float) (valueStrings.size() - 1))].substring (0, maximumLength);

            auto text = owner.getParameterText (index, normalisedValue);

            if (text.isEmpty())
                text = juce::String (normalisedValue, 2);

            return text.substring (0, maximumLength);
        }

        float getValueForText (const juce::String& text) const override
        {
            if (valueStrings.size() > 1)
                if (const auto at = valueStrings.indexOf (text); at >= 0)
                    return (float) at / (float) (valueStrings.size() - 1);

            return owner.getParameterValueForText (index, text);
        }

    private:
        RemotePlugin& owner;
        const int index;
        juce::String name, label;
        const juce::String parameterId;
        const float defaultValue;
        const int steps;
        const bool discrete, boolean, automatable, meta, inverted;
        const Category category;
        juce::StringArray valueStrings;
        std::atomic<float> value;
    };

    //==============================================================================
    struct RemotePlugin::Link
    {
        juce::String name;
        std::unique_ptr<platform::SharedMemory> memory;
        std::unique_ptr<platform::Signal> request, done;
        protocol::Block* block = nullptr;

        static std::unique_ptr<Link> create (Sandbox::Shared& shared)
        {
            auto link = std::make_unique<Link>();
            link->name = shared.makeName();
            link->memory = platform::SharedMemory::create (link->name, sizeof (protocol::Block));
            link->request = platform::Signal::create (protocol::getRequestSignalName (link->name));
            link->done = platform::Signal::create (protocol::getDoneSignalName (link->name));

            if (link->memory == nullptr || link->request == nullptr || link->done == nullptr)
                return {};

            link->block = static_cast<protocol::Block*> (link->memory->getData());
            return link;
        }

        /** As it was before the first block: for a new process. */
        void clear() noexcept
        {
            block->requested.store (0);
            block->done.store (0);
            block->flags = 0;
            block->toPlugin.clear();
            block->fromPlugin.clear();
            request->clear();
            done->clear();
        }
    };

    //==============================================================================
    juce::AudioProcessor::BusesProperties RemotePlugin::makeBuses (const juce::ValueTree& info)
    {
        BusesProperties buses;

        for (const auto& bus : info.getChildWithName (property::buses))
            buses.addBus (bus[property::input], bus[property::name].toString(),
                          protocol::channelsFromString (bus[property::channels].toString()), bus[property::enabled]);

        return buses;
    }

    std::unique_ptr<RemotePlugin> RemotePlugin::load (std::shared_ptr<Sandbox::Shared> shared, const juce::PluginDescription& description,
                                                      double sampleRate, int blockSize, juce::String& error)
    {
        auto link = Link::create (*shared);

        if (link == nullptr)
        {
            error = "Harmonia could not set up the memory to run the plugin in";
            return {};
        }

        Connection connection;
        const auto info = start (*shared, *link, connection, description, sampleRate, blockSize, error);

        if (! info.isValid())
            return {};

        if (countEnabledChannels (info) > protocol::maxChannels)
        {
            error = "The plugin uses more than " + juce::String (protocol::maxChannels) + " channels";
            connection.channel->send (juce::ValueTree (message::quit));
            connection.channel->close();
            shared->retire (std::move (connection.process));
            return {};
        }

        return std::unique_ptr<RemotePlugin> (new RemotePlugin (std::move (shared), std::move (link), std::move (connection), info));
    }

    juce::ValueTree RemotePlugin::start (Sandbox::Shared& shared, Link& link, Connection& connection,
                                         const juce::PluginDescription& description, double sampleRate, int blockSize,
                                         juce::String& error)
    {
        const auto pipeName = shared.makeName();
        connection.process = platform::Process::start (shared.options.executable,
                                                       { protocol::hostArgument, pipeName, link.name,
                                                         juce::String (platform::getCurrentProcessId()) });

        if (connection.process == nullptr)
        {
            error = "The plugin's process could not be started";
            return {};
        }

        // A plugin may show a window while loading (its licence...).
        connection.process->allowToComeToFront();

        connection.channel = std::make_shared<Channel> (false);
        connection.channel->setHandlers ([] (const juce::ValueTree& note)
        {
            if (note.hasType (message::log))
                juce::Logger::writeToLog (note[property::text].toString());
        }, {});

        for (const auto startedAt = now(); ! connection.channel->connect (pipeName);)
        {
            if (! connection.process->isRunning())
            {
                error = "The plugin's process ended as it started";
                return {};
            }

            if (now() - startedAt > connectTimeoutMs)
            {
                connection.process->kill();
                error = "The plugin's process did not answer";
                return {};
            }

            juce::Thread::sleep (2);
        }

        auto reply = connection.channel->call (juce::ValueTree (message::load, { { property::description, toXml (description) },
                                                                                 { property::sampleRate, sampleRate },
                                                                                 { property::blockSize, blockSize } }),
                                               loadTimeoutMs);

        if (! reply.isValid())
        {
            error = connection.process->isRunning() ? "The plugin took too long to load" : "The plugin crashed while loading";
            connection.process->kill();
            connection.channel->close();
            return {};
        }

        if (! reply[property::ok])
        {
            error = reply[property::error].toString();
            connection.channel->send (juce::ValueTree (message::quit));
            connection.channel->close();
            shared.retire (std::move (connection.process));
            return {};
        }

        return reply;
    }

    RemotePlugin::RemotePlugin (std::shared_ptr<Sandbox::Shared> s, std::unique_ptr<Link> l, Connection c, const juce::ValueTree& info)
        : juce::AudioPluginInstance (makeBuses (info)),
          shared (std::move (s)), link (std::move (l)), block (*link->block)
    {
        if (auto xml = juce::parseXML (info[property::description].toString()))
            description.loadFromXml (*xml);

        editorAvailable = info[property::hasEditor];
        midiIn = info[property::acceptsMidi];
        midiOut = info[property::producesMidi];
        midiEffect = info[property::isMidiEffect];
        mpe = info[property::supportsMPE];
        tailSeconds = info[property::tail];
        programNames = readPrograms (info.getChildWithName (property::programs));
        currentProgram = info[property::program];
        setLatencySamples (info[property::latency]);

        int index = 0;

        for (const auto& parameterInfo : info.getChildWithName (property::parameters))
        {
            auto parameter = std::make_unique<RemoteParameter> (*this, index++, parameterInfo);
            remoteParameters.add (parameter.get());
            addHostedParameter (std::move (parameter));
        }

        midiOutput.ensureSize (2 * protocol::midiCapacity);
        shared->add (this);
        attach (std::move (c));
        startTimerHz (30);
    }

    RemotePlugin::~RemotePlugin()
    {
        stopTimer();
        cancelPendingUpdate();
        shared->remove (this);

        Connection old;

        {
            const juce::ScopedLock sl (connectionLock);
            old = std::move (connection);
        }

        if (old.channel != nullptr)
        {
            old.channel->setHandlers ({}, {});

            if (! crashed)
                old.channel->send (juce::ValueTree (message::quit));

            old.channel->close();
        }

        if (old.process != nullptr)
            shared->retire (std::move (old.process));
    }

    //==============================================================================
    void RemotePlugin::attach (Connection fresh)
    {
        {
            const juce::ScopedLock sl (connectionLock);
            connection = std::move (fresh);
        }

        auto channel = getChannel();
        channel->setHandlers ([this] (const juce::ValueTree& note) { receive (note); },
                              [this] { markCrashed(); });

        // It may have ended before it was told where to report.
        if (! channel->isOpen())
            markCrashed();
    }

    std::shared_ptr<Channel> RemotePlugin::getChannel() const
    {
        const juce::ScopedLock sl (connectionLock);
        return connection.channel;
    }

    juce::ValueTree RemotePlugin::call (juce::ValueTree request, int timeoutMs) const
    {
        if (auto channel = getChannel())
            return channel->call (std::move (request), timeoutMs);

        return {};
    }

    void RemotePlugin::markCrashed()
    {
        if (crashed.exchange (true))
            return;

        // An audio thread waiting for a block stops waiting.
        link->done->raise();

        {
            const juce::ScopedLock sl (incomingLock);
            incoming.push_back (juce::ValueTree ("crashed"));
        }

        triggerAsyncUpdate();
    }

    void RemotePlugin::receive (const juce::ValueTree& note)
    {
        if (note.hasType (message::log))
        {
            juce::Logger::writeToLog (note[property::text].toString());
            return;
        }

        {
            const juce::ScopedLock sl (incomingLock);
            incoming.push_back (note);
        }

        triggerAsyncUpdate();
    }

    void RemotePlugin::handleAsyncUpdate()
    {
        std::vector<juce::ValueTree> notes;

        {
            const juce::ScopedLock sl (incomingLock);
            notes.swap (incoming);
        }

        for (const auto& note : notes)
            handleNote (note);
    }

    void RemotePlugin::handleNote (const juce::ValueTree& note)
    {
        if (note.hasType ("crashed"))
        {
            if (! crashed)
                return;

            if (editorShowing)
            {
                editorShowing = false;

                // A copy: the callback may delete what set it.
                if (auto closed = onEditorClosed)
                    closed ({});
            }

            shared->listeners.call ([this] (Sandbox::Listener& l) { l.pluginCrashed (*this); });
        }
        else if (note.hasType (message::changed))
        {
            applyLatency (note);
            currentProgram = note[property::program];

            auto details = ChangeDetails().withNonParameterStateChanged (note[property::stateChanged]);

            if (auto programs = note.getChildWithName (property::programs); programs.isValid())
            {
                programNames = readPrograms (programs);
                details = details.withProgramChanged (true);
            }

            if (auto names = note.getChildWithName (property::parameters); names.isValid())
            {
                for (int i = 0; i < juce::jmin (names.getNumChildren(), remoteParameters.size()); ++i)
                    remoteParameters[i]->rename (names.getChild (i)[property::name].toString(),
                                                 names.getChild (i)[property::label].toString());

                details = details.withParameterInfoChanged (true);
            }

            if (details.programChanged || details.parameterInfoChanged || details.nonParameterStateChanged)
                updateHostDisplay (details);
        }
        else if (note.hasType (message::gesture))
        {
            // Values the plugin changed before the gesture ended come first.
            timerCallback();

            if (auto* parameter = remoteParameters[(int) note[property::index]])
            {
                if (note[property::begin])
                    parameter->beginChangeGesture();
                else
                    parameter->endChangeGesture();
            }
        }
        else if (note.hasType (message::editorClosed))
        {
            if (editorShowing)
            {
                editorShowing = false;

                if (auto closed = onEditorClosed)
                    closed (readBounds (note));
            }
        }
    }

    void RemotePlugin::timerCallback()
    {
        block.fromPlugin.collect ([this] (int index, float value)
        {
            if (auto* parameter = remoteParameters[index])
                parameter->setValueFromPlugin (value);
        });
    }

    void RemotePlugin::applyLatency (const juce::ValueTree& reply)
    {
        if (reply.hasProperty (property::tail))
            tailSeconds = reply[property::tail];

        if (reply.hasProperty (property::latency))
            if (const int latency = reply[property::latency]; latency != getLatencySamples())
                setLatencySamples (latency);
    }

    //==============================================================================
    juce::String RemotePlugin::reload()
    {
        if (! crashed)
            return {};

        reloading = true;
        Connection old;

        {
            const juce::ScopedLock sl (connectionLock);
            old = std::move (connection);
        }

        if (old.channel != nullptr)
        {
            old.channel->setHandlers ({}, {});
            old.channel->close();
        }

        if (old.process != nullptr)
        {
            old.process->kill();
            old.process->waitForExit (2000);
        }

        // No audio thread may be inside processBlock while the shared memory starts over.
        for (const auto until = now() + 5000; activeProcessCalls.load() > 0 && now() < until;)
            juce::Thread::sleep (1);

        link->clear();

        Connection fresh;
        juce::String error;
        const auto info = start (*shared, *link, fresh, description, preparedRate > 0 ? preparedRate : getSampleRate(),
                                 preparedBlockSize > 0 ? preparedBlockSize : getBlockSize(), error);

        if (! info.isValid())
        {
            reloading = false;
            return error;
        }

        if (info.getChildWithName (property::parameters).getNumChildren() != remoteParameters.size())
        {
            fresh.channel->send (juce::ValueTree (message::quit));
            fresh.channel->close();
            shared->retire (std::move (fresh.process));
            reloading = false;
            return "The plugin is not the same as before (its parameters changed)";
        }

        attach (std::move (fresh));

        if (! getChannel()->isOpen())
        {
            reloading = false;
            return "The plugin crashed again as it started";
        }

        juce::MemoryBlock state;

        {
            const juce::ScopedLock sl (stateLock);
            state = lastState;
        }

        if (state.getSize() > 0)
            call (juce::ValueTree (message::setState, { { property::data, state } }), stateTimeoutMs);

        juce::ValueTree layoutRequest (message::setLayout);
        layoutRequest.appendChild (protocol::layoutToTree (getBusesLayout()), nullptr);
        call (layoutRequest, setupTimeoutMs);

        // The values Harmonia has (its automation, its knobs) win over the ones in the state: what
        // the plugin reported while taking its state back is not taken for a change.
        if (auto values = call (juce::ValueTree (message::getValues), stateTimeoutMs); values[property::ok])
        {
            block.fromPlugin.clear();

            if (auto* data = values[property::data].getBinaryData())
            {
                const auto count = juce::jmin ((int) (data->getSize() / sizeof (float)), remoteParameters.size());

                for (int i = 0; i < count; ++i)
                {
                    float theirs = 0;
                    std::memcpy (&theirs, static_cast<const char*> (data->getData()) + (size_t) i * sizeof (float), sizeof (float));

                    if (const auto ours = remoteParameters[i]->getValue(); ours != theirs)
                        parameterSet (i, ours);
                }
            }
        }

        if (prepared)
            applyLatency (call (juce::ValueTree (message::prepare, { { property::sampleRate, preparedRate },
                                                                     { property::blockSize, preparedBlockSize } }),
                                setupTimeoutMs));

        resetPending = true;
        missedBlocks = 0;
        hung = false;
        crashed = false;

        if (trackProperties.has_value())
            updateTrackProperties (*trackProperties);

        reloading = false;

        // A crash while it was being set up went unnoticed (it counted as the old one).
        if (! getChannel()->isOpen())
        {
            markCrashed();
            return "The plugin crashed again as it started";
        }

        shared->listeners.call ([this] (Sandbox::Listener& l) { l.pluginReloaded (*this); });
        return {};
    }

    //==============================================================================
    void RemotePlugin::prepareToPlay (double sampleRate, int maximumBlockSize)
    {
        setRateAndBufferSizeDetails (sampleRate, maximumBlockSize);
        prepared = true;
        preparedRate = sampleRate;
        preparedBlockSize = maximumBlockSize;

        if (! crashed)
            applyLatency (call (juce::ValueTree (message::prepare, { { property::sampleRate, sampleRate },
                                                                     { property::blockSize, maximumBlockSize } }),
                                setupTimeoutMs));
    }

    void RemotePlugin::releaseResources()
    {
        prepared = false;

        if (! crashed)
            call (juce::ValueTree (message::release), setupTimeoutMs);
    }

    void RemotePlugin::reset()
    {
        resetPending = true;
    }

    void RemotePlugin::setNonRealtime (bool isNonRealtime) noexcept
    {
        juce::AudioPluginInstance::setNonRealtime (isNonRealtime);
    }

    void RemotePlugin::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        process (buffer, midi, false);
    }

    void RemotePlugin::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        process (buffer, midi, true);
    }

    void RemotePlugin::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        const ActiveCall active (activeProcessCalls);
        const auto numSamples = buffer.getNumSamples();

        if (crashed.load() || reloading.load() || ! prepared || buffer.getNumChannels() > protocol::maxChannels)
        {
            stayQuiet (buffer, 0);
            midi.clear();
            return;
        }

        juce::Optional<juce::AudioPlayHead::PositionInfo> position;

        if (auto* head = getPlayHead())
            position = head->getPosition();

        midiOutput.clear();

        for (int start = 0; start < numSamples; start += protocol::maxSamples)
        {
            if (! processPart (buffer, buffer.getNumChannels(), start, juce::jmin (protocol::maxSamples, numSamples - start),
                               midi, position, bypassed))
            {
                ++missedBlocks;
                stayQuiet (buffer, start);
                break;
            }
        }

        midi.swapWith (midiOutput);
    }

    bool RemotePlugin::processPart (juce::AudioBuffer<float>& buffer, int numChannels, int start, int numSamples,
                                    const juce::MidiBuffer& midi, const juce::Optional<juce::AudioPlayHead::PositionInfo>& position, bool bypassed)
    {
        const auto previous = block.requested.load (std::memory_order_relaxed);

        // Still busy with a block it ran out of time for: this one is skipped.
        if (block.done.load (std::memory_order_acquire) != previous)
            return false;

        requestStartMs = 0;

        block.numChannels = numChannels;
        block.numSamples = numSamples;

        for (int channel = 0; channel < numChannels; ++channel)
            std::memcpy (block.audio[channel], buffer.getReadPointer (channel, start), (size_t) numSamples * sizeof (float));

        const auto offline = isNonRealtime();
        block.flags = (bypassed ? protocol::flags::bypassed : 0u)
                    | (offline ? protocol::flags::nonRealtime : 0u)
                    | (resetPending.exchange (false) ? protocol::flags::reset : 0u);
        block.hasPosition = position.hasValue() ? 1 : 0;

        if (position.hasValue())
            block.position = advance (*position, start, getSampleRate());

        block.midiIn.write (midi, start, numSamples);

        const auto id = previous + 1;
        const auto startedAt = now();
        requestIsOffline = offline;
        requestStartMs = startedAt;
        block.requested.store (id, std::memory_order_release);
        link->request->raise();

        // Live, a block late by more than its own length is lost anyway; rendering, it waits
        // as long as it takes (the watchdog stops a plugin that hangs).
        const auto blockMs = getSampleRate() > 0 ? 1000.0 * numSamples / getSampleRate() : 10.0;
        const auto deadline = offline ? std::numeric_limits<double>::max() : juce::jlimit (2.0, 100.0, blockMs);

        while (block.done.load (std::memory_order_acquire) != id)
        {
            const auto left = deadline - (now() - startedAt);

            if (crashed.load() || left <= 0)
                return false;

            link->done->wait (juce::jlimit (1, 50, (int) std::ceil (left)));
        }

        requestStartMs = 0;

        for (int channel = 0; channel < numChannels; ++channel)
            std::memcpy (buffer.getWritePointer (channel, start), block.audio[channel], (size_t) numSamples * sizeof (float));

        block.midiOut.read (midiOutput, start);
        return true;
    }

    void RemotePlugin::stayQuiet (juce::AudioBuffer<float>& buffer, int fromSample)
    {
        const auto numSamples = buffer.getNumSamples() - fromSample;

        if (numSamples <= 0)
            return;

        // An instrument is silent; an effect lets its input through (its extra outputs are silent).
        const auto inputs = getTotalNumInputChannels();

        for (int channel = inputs; channel < buffer.getNumChannels(); ++channel)
            buffer.clear (channel, fromSample, numSamples);
    }

    void RemotePlugin::checkForHang (double nowMs)
    {
        const auto startedAt = requestStartMs.load();

        if (startedAt <= 0 || crashed.load())
            return;

        const auto limit = (double) shared->options.hangLimitMs * (requestIsOffline.load() ? 10.0 : 1.0);

        if (nowMs - startedAt < limit
             || block.done.load (std::memory_order_acquire) == block.requested.load (std::memory_order_acquire))
            return;

        hung = true;

        const juce::ScopedLock sl (connectionLock);

        if (connection.process != nullptr)
            connection.process->kill();
    }

    //==============================================================================
    void RemotePlugin::parameterSet (int index, float value) noexcept
    {
        if (index < protocol::maxParameters)
            block.toPlugin.post (index, value);
    }

    juce::String RemotePlugin::getParameterText (int index, float value) const
    {
        const auto key = std::make_pair (index, value);

        {
            const juce::ScopedLock sl (cacheLock);

            if (auto found = textCache.find (key); found != textCache.end())
                return found->second;

            if (crashed || now() < textUnavailableUntilMs)
                return {};
        }

        const auto reply = call (juce::ValueTree (message::getText, { { property::index, index }, { property::value, value } }),
                                 textTimeoutMs);
        const juce::ScopedLock sl (cacheLock);

        if (! reply[property::ok])
        {
            // A busy process (a dialog of its own open) isn't asked again for a moment.
            textUnavailableUntilMs = now() + 2000;
            return {};
        }

        if (textCache.size() > 4096)
            textCache.clear();

        const auto text = reply[property::text].toString();
        textCache[key] = text;
        return text;
    }

    float RemotePlugin::getParameterValueForText (int index, const juce::String& text) const
    {
        if (! crashed)
            if (const auto reply = call (juce::ValueTree (message::getValueForText, { { property::index, index },
                                                                                       { property::text, text } }),
                                         askTimeoutMs);
                reply[property::ok])
                return reply[property::value];

        return juce::jlimit (0.0f, 1.0f, text.getFloatValue());
    }

    //==============================================================================
    void RemotePlugin::setCurrentProgram (int index)
    {
        currentProgram = index;

        if (! crashed)
            if (const auto reply = call (juce::ValueTree (message::setProgram, { { property::index, index } }), askTimeoutMs);
                reply[property::ok])
                currentProgram = reply[property::program];
    }

    void RemotePlugin::changeProgramName (int index, const juce::String& newName)
    {
        if (! juce::isPositiveAndBelow (index, programNames.size()))
            return;

        programNames.set (index, newName);

        if (! crashed)
            call (juce::ValueTree (message::renameProgram, { { property::index, index }, { property::name, newName } }), askTimeoutMs);
    }

    void RemotePlugin::getStateInformation (juce::MemoryBlock& destination)
    {
        if (! crashed)
        {
            if (const auto reply = call (juce::ValueTree (message::getState), stateTimeoutMs); reply[property::ok])
            {
                if (auto* data = reply[property::data].getBinaryData())
                {
                    const juce::ScopedLock sl (stateLock);
                    lastState = *data;
                }
            }
        }

        // After a crash: the state it had last, so saving the song keeps the plugin's settings.
        const juce::ScopedLock sl (stateLock);
        destination = lastState;
    }

    void RemotePlugin::setStateInformation (const void* data, int sizeInBytes)
    {
        const juce::MemoryBlock state (data, (size_t) juce::jmax (0, sizeInBytes));

        {
            const juce::ScopedLock sl (stateLock);
            lastState = state;
        }

        if (! crashed)
            if (const auto reply = call (juce::ValueTree (message::setState, { { property::data, state } }), stateTimeoutMs);
                reply[property::ok])
                currentProgram = reply[property::program];
    }

    void RemotePlugin::updateTrackProperties (const TrackProperties& properties)
    {
        trackProperties = properties;

        if (auto channel = getChannel(); channel != nullptr && ! crashed)
            channel->send (juce::ValueTree (message::trackProperties,
                                            { { property::name, properties.name.value_or (juce::String()) },
                                              { property::colour, (juce::int64) properties.colourARGB.value_or (0) } }));
    }

    bool RemotePlugin::isBusesLayoutSupported (const BusesLayout& layout) const
    {
        if (countChannels (layout.inputBuses) > protocol::maxChannels || countChannels (layout.outputBuses) > protocol::maxChannels)
            return false;

        auto tree = protocol::layoutToTree (layout);
        const auto key = tree.toXmlString (juce::XmlElement::TextFormat().singleLine());

        {
            const juce::ScopedLock sl (cacheLock);

            if (auto found = layoutCache.find (key); found != layoutCache.end())
                return found->second;
        }

        if (crashed)
            return layout == getBusesLayout();

        juce::ValueTree request (message::isLayoutSupported);
        request.appendChild (tree, nullptr);
        const auto reply = call (request, askTimeoutMs);

        if (! reply.isValid())
            return layout == getBusesLayout();

        const bool supported = reply[property::ok];
        const juce::ScopedLock sl (cacheLock);
        layoutCache[key] = supported;
        return supported;
    }

    void RemotePlugin::processorLayoutsChanged()
    {
        if (crashed)
            return;

        juce::ValueTree request (message::setLayout);
        request.appendChild (protocol::layoutToTree (getBusesLayout()), nullptr);
        applyLatency (call (request, setupTimeoutMs));
    }

    //==============================================================================
    bool RemotePlugin::showEditor (juce::Point<int> topLeft, const juce::String& title, bool bringToFront)
    {
        if (crashed || ! editorAvailable)
            return false;

        {
            const juce::ScopedLock sl (connectionLock);

            if (connection.process != nullptr)
                connection.process->allowToComeToFront();
        }

        const auto reply = call (juce::ValueTree (message::showEditor, { { property::x, topLeft.x }, { property::y, topLeft.y },
                                                                         { property::name, title },
                                                                         { property::toFront, bringToFront },
                                                                         { property::onTop, editorWantedOnTop } }),
                                 askTimeoutMs * 2);
        editorShowing = reply[property::ok];
        editorOnTop = reply[property::onTop];
        return editorShowing;
    }

    void RemotePlugin::setEditorAlwaysOnTop (bool shouldBeOnTop)
    {
        editorWantedOnTop = shouldBeOnTop;

        // An open window is told now (asked to show again it stays where it is).
        if (editorShowing && ! crashed)
            showEditor ({}, {}, false);
    }

    juce::Rectangle<int> RemotePlugin::hideEditor()
    {
        if (! editorShowing)
            return {};

        editorShowing = false;

        if (crashed)
            return {};

        const auto reply = call (juce::ValueTree (message::hideEditor), askTimeoutMs);
        return reply[property::ok] ? readBounds (reply) : juce::Rectangle<int>();
    }
}
