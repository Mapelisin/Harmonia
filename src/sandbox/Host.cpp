#include "sandbox/Host.h"
#include "sandbox/Channel.h"
#include "sandbox/Platform.h"
#include "sandbox/Protocol.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::sandbox
{
    namespace
    {
        namespace message = protocol::message;
        namespace property = protocol::property;

        /** True on the audio thread while it applies the values Harmonia set: those aren't sent back. */
        thread_local bool applyingHarmoniaValues = false;

        /** The song position Harmonia sent with the block. */
        class SharedPlayHead final : public juce::AudioPlayHead
        {
        public:
            explicit SharedPlayHead (const protocol::Block*& b) : block (b) {}

            juce::Optional<PositionInfo> getPosition() const override
            {
                if (block == nullptr || block->hasPosition == 0)
                    return {};

                return block->position;
            }

        private:
            const protocol::Block*& block;
        };

        /** The plugin's own window, in this process. */
        class EditorWindow final : public juce::DocumentWindow
        {
        public:
            EditorWindow (const juce::String& title, juce::AudioProcessorEditor& editor, std::function<void()> onClose)
                : juce::DocumentWindow (title, juce::Colours::black, juce::DocumentWindow::closeButton, true),
                  close (std::move (onClose))
            {
                setUsingNativeTitleBar (true);
                setContentNonOwned (&editor, true);
                setResizable (editor.isResizable(), false);
            }

            void closeButtonPressed() override    { close(); }

        private:
            std::function<void()> close;
        };

        /** What this process logs (in Debug: JUCE's assertion failures) goes to Harmonia's log. */
        class ForwardingLogger final : public juce::Logger
        {
        public:
            explicit ForwardingLogger (Channel& c) : channel (c) {}

            void logMessage (const juce::String& text) override
            {
                channel.send (juce::ValueTree (message::log, { { property::text, text } }));
            }

        private:
            Channel& channel;
        };

        juce::ValueTree replyWith (std::initializer_list<juce::NamedValueSet::NamedValue> properties)
        {
            return juce::ValueTree (message::reply, properties);
        }

        juce::ValueTree failure (const juce::String& error)
        {
            return replyWith ({ { property::ok, false }, { property::error, error } });
        }

        juce::String getParameterId (juce::AudioProcessorParameter& parameter)
        {
            if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (&parameter))
                return hosted->getParameterID();

            return juce::String (parameter.getParameterIndex());
        }
    }

    //==============================================================================
    class Host::Impl final : private juce::AudioProcessorListener,
                             private juce::AsyncUpdater,
                             private juce::Thread
    {
    public:
        explicit Impl (Options o)
            : juce::Thread ("Plugin audio"), options (std::move (o))
        {
            memory = platform::SharedMemory::open (options.memoryName, sizeof (protocol::Block));
            requestSignal = platform::Signal::open (protocol::getRequestSignalName (options.memoryName));
            doneSignal = platform::Signal::open (protocol::getDoneSignalName (options.memoryName));

            if (memory == nullptr || requestSignal == nullptr || doneSignal == nullptr || ! channel.listen (options.pipeName))
            {
                finish();
                return;
            }

            block = static_cast<protocol::Block*> (memory->getData());
            sharedBlock = block;

            for (int i = 0; i < protocol::maxChannels; ++i)
                channels[i] = block->audio[i];

            if (options.addFormats != nullptr)
                options.addFormats (formats);

            channel.setHandlers ([this] (const juce::ValueTree& m) { handle (m); }, {});

            previousLogger = juce::Logger::getCurrentLogger();
            juce::Logger::setCurrentLogger (&logger);

            // Harmonia ending (even by crashing) ends this process too.
            watcher = std::make_unique<platform::ProcessWatcher> (options.harmoniaProcessId,
                                                                  [finished = options.onFinished]
                                                                  {
                                                                      juce::MessageManager::callAsync (finished);
                                                                  });
        }

        ~Impl() override
        {
            watcher.reset();
            stopThread (2000);
            cancelPendingUpdate();
            closeEditor();

            if (plugin != nullptr)
            {
                plugin->removeListener (this);
                plugin->releaseResources();
                plugin.reset();
            }

            if (juce::Logger::getCurrentLogger() == &logger)
                juce::Logger::setCurrentLogger (previousLogger);

            channel.close();
        }

    private:
        Options options;
        std::unique_ptr<platform::SharedMemory> memory;
        std::unique_ptr<platform::Signal> requestSignal, doneSignal;
        protocol::Block* block = nullptr;
        const protocol::Block* sharedBlock = nullptr;
        float* channels[protocol::maxChannels] {};

        Channel channel { true };
        ForwardingLogger logger { channel };
        juce::Logger* previousLogger = nullptr;
        std::unique_ptr<platform::ProcessWatcher> watcher;

        juce::AudioPluginFormatManager formats;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::Array<juce::AudioProcessorParameter*> parameters;
        SharedPlayHead playHead { sharedBlock };

        juce::CriticalSection processLock;
        bool prepared = false;
        bool nonRealtime = false;
        juce::MidiBuffer midi;

        std::unique_ptr<juce::AudioProcessorEditor> editor;
        std::unique_ptr<EditorWindow> window;

        std::atomic<bool> programChanged { false }, parameterInfoChanged { false }, stateChanged { false };

        void finish()
        {
            juce::MessageManager::callAsync (options.onFinished);
        }

        //==============================================================================
        void handle (const juce::ValueTree& request)
        {
            const auto type = request.getType();

            if (type == message::quit)
            {
                finish();
                return;
            }

            if (type == message::trackProperties)
            {
                if (plugin != nullptr)
                    plugin->updateTrackProperties ({ request[property::name].toString(),
                                                     (juce::uint32) (juce::int64) request[property::colour] });
                return;
            }

            if (type == message::load)
            {
                channel.reply (request, load (request));
                return;
            }

            if (plugin == nullptr)
            {
                channel.reply (request, failure ("No plugin is loaded"));
                return;
            }

            // Values Harmonia set since the last block reach the plugin before it's asked anything
            // (its state is saved while nothing plays, say).
            {
                const juce::ScopedLock sl (processLock);
                applyHarmoniaValues();
            }

            if (type == message::prepare)               channel.reply (request, prepare (request));
            else if (type == message::release)          channel.reply (request, release());
            else if (type == message::getState)         channel.reply (request, getState());
            else if (type == message::setState)         channel.reply (request, setState (request));
            else if (type == message::getValues)        channel.reply (request, getValues());
            else if (type == message::setProgram)       channel.reply (request, setProgram (request));
            else if (type == message::renameProgram)    channel.reply (request, renameProgram (request));
            else if (type == message::isLayoutSupported) channel.reply (request, isLayoutSupported (request));
            else if (type == message::setLayout)        channel.reply (request, setLayout (request));
            else if (type == message::getText)          channel.reply (request, getText (request));
            else if (type == message::getValueForText)  channel.reply (request, getValueForText (request));
            else if (type == message::showEditor)       channel.reply (request, showEditor (request));
            else if (type == message::hideEditor)       channel.reply (request, hideEditor());
        }

        juce::ValueTree load (const juce::ValueTree& request)
        {
            if (plugin != nullptr)
                return failure ("A plugin is already loaded");

            juce::PluginDescription description;

            if (auto xml = juce::parseXML (request[property::description].toString()))
                description.loadFromXml (*xml);

            juce::String error;
            plugin = formats.createPluginInstance (description, request[property::sampleRate],
                                                   request[property::blockSize], error);

            if (plugin == nullptr)
                return failure (error.isNotEmpty() ? error : juce::String ("The plugin could not be loaded"));

            plugin->setPlayHead (&playHead);
            parameters = plugin->getParameters();
            plugin->addListener (this);
            startRealtimeThread (juce::Thread::RealtimeOptions().withPriority (10));

            return describe();
        }

        /** Everything Harmonia needs to stand in for the plugin. */
        juce::ValueTree describe()
        {
            auto reply = replyWith ({ { property::ok, true },
                                      { property::hasEditor, plugin->hasEditor() },
                                      { property::acceptsMidi, plugin->acceptsMidi() },
                                      { property::producesMidi, plugin->producesMidi() },
                                      { property::isMidiEffect, plugin->isMidiEffect() },
                                      { property::supportsMPE, plugin->supportsMPE() },
                                      { property::tail, plugin->getTailLengthSeconds() },
                                      { property::latency, plugin->getLatencySamples() },
                                      { property::program, plugin->getCurrentProgram() } });

            if (auto xml = plugin->getPluginDescription().createXml())
                reply.setProperty (property::description, xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);

            reply.appendChild (describePrograms(), nullptr);

            juce::ValueTree buses (property::buses);

            for (const auto isInput : { true, false })
            {
                for (int i = 0; i < plugin->getBusCount (isInput); ++i)
                {
                    auto* bus = plugin->getBus (isInput, i);
                    const auto layout = bus->isEnabled() ? bus->getCurrentLayout() : bus->getDefaultLayout();

                    buses.appendChild (juce::ValueTree (property::bus, { { property::input, isInput },
                                                                         { property::name, bus->getName() },
                                                                         { property::channels, protocol::channelsToString (layout) },
                                                                         { property::enabled, bus->isEnabled() } }), nullptr);
                }
            }

            reply.appendChild (buses, nullptr);

            juce::ValueTree list (property::parameters);

            for (auto* parameter : parameters)
            {
                if (list.getNumChildren() >= protocol::maxParameters)
                    break;

                list.appendChild (juce::ValueTree (property::parameter,
                    { { property::name, parameter->getName (1024) },
                      { property::label, parameter->getLabel() },
                      { property::parameterId, getParameterId (*parameter) },
                      { property::defaultValue, parameter->getDefaultValue() },
                      { property::value, parameter->getValue() },
                      { property::steps, parameter->getNumSteps() },
                      { property::discrete, parameter->isDiscrete() },
                      { property::boolean, parameter->isBoolean() },
                      { property::automatable, parameter->isAutomatable() },
                      { property::meta, parameter->isMetaParameter() },
                      { property::inverted, parameter->isOrientationInverted() },
                      { property::category, (int) parameter->getCategory() },
                      { property::valueStrings, parameter->getAllValueStrings().joinIntoString ("\n") } }), nullptr);
            }

            reply.appendChild (list, nullptr);
            return reply;
        }

        juce::ValueTree describePrograms()
        {
            juce::ValueTree programs (property::programs);

            for (int i = 0; i < plugin->getNumPrograms(); ++i)
                programs.appendChild (juce::ValueTree (property::program, { { property::name, plugin->getProgramName (i) } }), nullptr);

            return programs;
        }

        juce::ValueTree prepare (const juce::ValueTree& request)
        {
            const double sampleRate = request[property::sampleRate];
            const int blockSize = request[property::blockSize];

            {
                const juce::ScopedLock sl (processLock);
                plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
                plugin->prepareToPlay (sampleRate, blockSize);
                midi.ensureSize (2 * protocol::midiCapacity);
                prepared = true;
            }

            return replyWith ({ { property::ok, true }, { property::latency, plugin->getLatencySamples() },
                                { property::tail, plugin->getTailLengthSeconds() } });
        }

        juce::ValueTree release()
        {
            const juce::ScopedLock sl (processLock);
            plugin->releaseResources();
            prepared = false;
            return replyWith ({ { property::ok, true } });
        }

        juce::ValueTree getState()
        {
            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            return replyWith ({ { property::ok, true }, { property::data, state } });
        }

        juce::ValueTree setState (const juce::ValueTree& request)
        {
            if (auto* state = request[property::data].getBinaryData(); state != nullptr && state->getSize() > 0)
                plugin->setStateInformation (state->getData(), (int) state->getSize());

            return replyWith ({ { property::ok, true }, { property::program, plugin->getCurrentProgram() } });
        }

        juce::ValueTree getValues()
        {
            juce::MemoryBlock values;
            const auto count = juce::jmin (parameters.size(), protocol::maxParameters);

            for (int i = 0; i < count; ++i)
            {
                const auto value = parameters[i]->getValue();
                values.append (&value, sizeof (value));
            }

            return replyWith ({ { property::ok, true }, { property::data, values } });
        }

        juce::ValueTree setProgram (const juce::ValueTree& request)
        {
            plugin->setCurrentProgram (request[property::index]);
            return replyWith ({ { property::ok, true }, { property::program, plugin->getCurrentProgram() } });
        }

        juce::ValueTree renameProgram (const juce::ValueTree& request)
        {
            plugin->changeProgramName (request[property::index], request[property::name].toString());
            return replyWith ({ { property::ok, true } });
        }

        juce::ValueTree isLayoutSupported (const juce::ValueTree& request)
        {
            const auto layout = protocol::layoutFromTree (request.getChildWithName (property::layout));
            return replyWith ({ { property::ok, plugin->checkBusesLayoutSupported (layout) } });
        }

        juce::ValueTree setLayout (const juce::ValueTree& request)
        {
            const auto layout = protocol::layoutFromTree (request.getChildWithName (property::layout));
            const juce::ScopedLock sl (processLock);
            const auto ok = plugin->setBusesLayout (layout);

            if (ok && prepared)
                plugin->prepareToPlay (plugin->getSampleRate(), plugin->getBlockSize());

            return replyWith ({ { property::ok, ok }, { property::latency, plugin->getLatencySamples() } });
        }

        juce::AudioProcessorParameter* getParameter (const juce::ValueTree& request) const
        {
            return parameters[(int) request[property::index]];
        }

        juce::ValueTree getText (const juce::ValueTree& request)
        {
            if (auto* parameter = getParameter (request))
                return replyWith ({ { property::ok, true },
                                    { property::text, parameter->getText (request[property::value], 1024) } });

            return failure ("No such parameter");
        }

        juce::ValueTree getValueForText (const juce::ValueTree& request)
        {
            if (auto* parameter = getParameter (request))
                return replyWith ({ { property::ok, true },
                                    { property::value, parameter->getValueForText (request[property::text].toString()) } });

            return failure ("No such parameter");
        }

        //==============================================================================
        juce::ValueTree describeWindow() const
        {
            const auto bounds = window != nullptr ? window->getBounds() : juce::Rectangle<int>();
            return replyWith ({ { property::ok, window != nullptr }, { property::x, bounds.getX() }, { property::y, bounds.getY() },
                                { property::width, bounds.getWidth() }, { property::height, bounds.getHeight() } });
        }

        juce::ValueTree showEditor (const juce::ValueTree& request)
        {
            if (window == nullptr)
            {
                editor.reset (plugin->createEditorAndMakeActive());

                if (editor == nullptr)
                    return failure ("The plugin has no window");

                window = std::make_unique<EditorWindow> (request[property::name].toString(), *editor,
                                                         [this] { editorClosedByUser(); });
                window->setTopLeftPosition (request[property::x], request[property::y]);
                window->setVisible (true);
            }

            if (request[property::toFront])
                window->toFront (true);

            return describeWindow();
        }

        juce::ValueTree hideEditor()
        {
            auto reply = describeWindow();
            closeEditor();
            return reply;
        }

        void closeEditor()
        {
            window.reset();

            // The plugin forgets its editor, or it would never make another.
            if (editor != nullptr && plugin != nullptr)
                plugin->editorBeingDeleted (editor.get());

            editor.reset();
        }

        void editorClosedByUser()
        {
            juce::ValueTree closed (message::editorClosed);
            closed.copyPropertiesFrom (describeWindow(), nullptr);
            channel.send (closed);

            // Not from inside the window's own button handling.
            juce::MessageManager::callAsync ([weak = juce::WeakReference<Impl> (this)]
            {
                if (weak != nullptr)
                    weak->closeEditor();
            });
        }

        //==============================================================================
        void run() override
        {
            while (! threadShouldExit())
            {
                if (! requestSignal->wait (100))
                    continue;

                const auto requested = block->requested.load (std::memory_order_acquire);

                if (requested == block->done.load (std::memory_order_relaxed))
                    continue;

                process();
                block->done.store (requested, std::memory_order_release);
                doneSignal->raise();
            }
        }

        void process()
        {
            juce::ScopedNoDenormals noDenormals;
            const juce::ScopedLock sl (processLock);
            block->midiOut.numBytes = 0;

            if (! prepared)
                return;

            const auto flags = block->flags;

            if (const auto rendering = (flags & protocol::flags::nonRealtime) != 0; rendering != nonRealtime)
            {
                nonRealtime = rendering;
                plugin->setNonRealtime (rendering);
            }

            if ((flags & protocol::flags::reset) != 0)
                plugin->reset();

            applyHarmoniaValues();

            midi.clear();
            block->midiIn.read (midi, 0);

            const auto numChannels = juce::jlimit (0, protocol::maxChannels, (int) block->numChannels);
            const auto numSamples = juce::jlimit (0, protocol::maxSamples, (int) block->numSamples);
            juce::AudioBuffer<float> buffer (channels, numChannels, numSamples);

            if ((flags & protocol::flags::bypassed) != 0)
                plugin->processBlockBypassed (buffer, midi);
            else
                plugin->processBlock (buffer, midi);

            block->midiOut.write (midi, 0, numSamples);
        }

        /** Under processLock. */
        void applyHarmoniaValues()
        {
            applyingHarmoniaValues = true;
            block->toPlugin.collect ([this] (int index, float value)
            {
                if (auto* parameter = parameters[index])
                    parameter->setValue (value);
            });
            applyingHarmoniaValues = false;
        }

        //==============================================================================
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value) override
        {
            if (! applyingHarmoniaValues && index >= 0 && index < protocol::maxParameters)
                block->fromPlugin.post (index, value);
        }

        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            // The latency goes with every note anyway.
            if (details.programChanged)        programChanged = true;
            if (details.parameterInfoChanged)  parameterInfoChanged = true;
            if (details.nonParameterStateChanged) stateChanged = true;

            triggerAsyncUpdate();
        }

        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override
        {
            channel.send (juce::ValueTree (message::gesture, { { property::index, index }, { property::begin, true } }));
        }

        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override
        {
            channel.send (juce::ValueTree (message::gesture, { { property::index, index }, { property::begin, false } }));
        }

        void handleAsyncUpdate() override
        {
            juce::ValueTree note (message::changed, { { property::latency, plugin->getLatencySamples() },
                                                      { property::program, plugin->getCurrentProgram() },
                                                      { property::stateChanged, stateChanged.exchange (false) } });

            if (programChanged.exchange (false))
                note.appendChild (describePrograms(), nullptr);

            if (parameterInfoChanged.exchange (false))
            {
                juce::ValueTree names (property::parameters);

                for (int i = 0; i < juce::jmin (parameters.size(), protocol::maxParameters); ++i)
                    names.appendChild (juce::ValueTree (property::parameter, { { property::name, parameters[i]->getName (1024) },
                                                                              { property::label, parameters[i]->getLabel() } }), nullptr);

                note.appendChild (names, nullptr);
            }

            channel.send (note);
        }

        JUCE_DECLARE_WEAK_REFERENCEABLE (Impl)
        JUCE_DECLARE_NON_COPYABLE (Impl)
    };

    //==============================================================================
    std::optional<Host::Options> Host::fromCommandLine (const juce::StringArray& arguments)
    {
        const auto at = arguments.indexOf (protocol::hostArgument);

        if (at < 0 || at + 3 >= arguments.size())
            return {};

        Options options;
        options.pipeName = arguments[at + 1].unquoted();
        options.memoryName = arguments[at + 2].unquoted();
        options.harmoniaProcessId = (uint32_t) arguments[at + 3].getLargeIntValue();
        return options;
    }

    Host::Host (Options options) : impl (std::make_unique<Impl> (std::move (options))) {}
    Host::~Host() = default;
}
