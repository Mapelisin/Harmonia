#include "sandbox/TestPlugins.h"

namespace harmonia::test::plugins
{
    namespace
    {
        juce::PluginDescription describe (const juce::String& name, bool instrument)
        {
            juce::PluginDescription description;
            description.name = description.descriptiveName = name;
            description.pluginFormatName = formatName;
            description.category = instrument ? "Synth" : "Effect";
            description.manufacturerName = "Harmonia tests";
            description.version = "1.0";
            description.fileOrIdentifier = juce::String (formatName) + ":" + name;
            description.uniqueId = description.deprecatedUid = instrument ? 0x48547331 : 0x48546731;
            description.isInstrument = instrument;
            description.numInputChannels = instrument ? 0 : 2;
            description.numOutputChannels = 2;
            return description;
        }

        class TestParameter final : public juce::AudioPluginInstance::HostedParameter
        {
        public:
            TestParameter (const juce::String& n, float defaultTo) : name (n), defaultValue (defaultTo), value (defaultTo) {}

            float getValue() const override                         { return value; }
            void setValue (float newValue) override                 { value = newValue; }
            float getDefaultValue() const override                  { return defaultValue; }
            juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
            juce::String getLabel() const override                  { return "%"; }
            juce::String getParameterID() const override            { return name; }

            juce::String getText (float v, int) const override      { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
            float getValueForText (const juce::String& text) const override    { return text.getFloatValue() / 100.0f; }

        private:
            const juce::String name;
            const float defaultValue;
            std::atomic<float> value;
        };

        class TestPlugin final : public juce::AudioPluginInstance
        {
        public:
            explicit TestPlugin (bool instrument)
                : juce::AudioPluginInstance (instrument ? BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())
                                                        : BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                                           .withOutput ("Out", juce::AudioChannelSet::stereo())),
                  isSynth (instrument)
            {
                level = add (isSynth ? "Level" : "Gain", 0.5f);
                crash = add ("Crash", 0.0f);
                hang = add ("Hang", 0.0f);

                if (isSynth)
                    lastNote = add ("Last note", 0.0f);
            }

            const juce::String getName() const override    { return isSynth ? "Test Synth" : "Test Gain"; }
            void fillInPluginDescription (juce::PluginDescription& d) const override    { d = isSynth ? synth() : gain(); }

            void prepareToPlay (double, int) override
            {
                if (isSynth)
                    setLatencySamples (64);
            }

            void releaseResources() override {}

            void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
            {
                if (crash->getValue() > 0.5f)
                {
                    volatile int* nowhere = nullptr;
                    *nowhere = 1;
                }

                while (hang->getValue() > 0.5f)
                    juce::Thread::sleep (5);

                if (! isSynth)
                {
                    buffer.applyGain (2.0f * level->getValue());
                    return;
                }

                int position = 0;

                for (const auto event : midi)
                {
                    fill (buffer, position, event.samplePosition);
                    position = event.samplePosition;
                    const auto message = event.getMessage();

                    if (message.isNoteOn())
                    {
                        ++held;
                        lastNote->setValueNotifyingHost ((float) message.getNoteNumber() / 127.0f);
                    }
                    else if (message.isNoteOff())
                    {
                        held = juce::jmax (0, held - 1);
                    }
                }

                fill (buffer, position, buffer.getNumSamples());
            }

            using juce::AudioPluginInstance::processBlock;

            double getTailLengthSeconds() const override    { return 0; }
            bool acceptsMidi() const override               { return isSynth; }
            bool producesMidi() const override              { return isSynth; }
            bool hasEditor() const override                 { return true; }
            juce::AudioProcessorEditor* createEditor() override    { return new juce::GenericAudioProcessorEditor (*this); }

            int getNumPrograms() override                   { return programs.size(); }
            int getCurrentProgram() override                { return program; }
            void setCurrentProgram (int index) override     { program = juce::jlimit (0, programs.size() - 1, index); }
            const juce::String getProgramName (int index) override    { return programs[index]; }
            void changeProgramName (int index, const juce::String& name) override    { programs.set (index, name); }

            void getStateInformation (juce::MemoryBlock& state) override
            {
                juce::XmlElement xml ("TEST");
                xml.setAttribute ("level", level->getValue());
                xml.setAttribute ("program", program);
                copyXmlToBinary (xml, state);
            }

            void setStateInformation (const void* data, int size) override
            {
                if (auto xml = getXmlFromBinary (data, size))
                {
                    level->setValueNotifyingHost ((float) xml->getDoubleAttribute ("level", 0.5));
                    program = xml->getIntAttribute ("program");
                }
            }

        protected:
            bool isBusesLayoutSupported (const BusesLayout& layout) const override
            {
                const auto out = layout.getMainOutputChannelSet();

                if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
                    return false;

                return isSynth ? layout.inputBuses.isEmpty() : layout.getMainInputChannelSet() == out;
            }

        private:
            const bool isSynth;
            juce::AudioProcessorParameter* level = nullptr;
            juce::AudioProcessorParameter* crash = nullptr;
            juce::AudioProcessorParameter* hang = nullptr;
            juce::AudioProcessorParameter* lastNote = nullptr;
            juce::StringArray programs { "One", "Two", "Three" };
            int program = 0;
            int held = 0;

            juce::AudioProcessorParameter* add (const juce::String& name, float defaultValue)
            {
                auto parameter = std::make_unique<TestParameter> (name, defaultValue);
                auto* raw = parameter.get();
                addHostedParameter (std::move (parameter));
                return raw;
            }

            void fill (juce::AudioBuffer<float>& buffer, int from, int to) const
            {
                const auto value = held > 0 ? level->getValue() : 0.0f;

                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    for (int i = from; i < to; ++i)
                        buffer.setSample (channel, i, value);
            }
        };

        class TestFormat final : public juce::AudioPluginFormat
        {
        public:
            juce::String getName() const override    { return formatName; }

            void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& identifier) override
            {
                for (const auto& description : { synth(), gain() })
                    if (description.fileOrIdentifier == identifier)
                        results.add (new juce::PluginDescription (description));
            }

            bool fileMightContainThisPluginType (const juce::String& identifier) override    { return identifier.startsWith (formatName); }
            juce::String getNameOfPluginFromIdentifier (const juce::String& identifier) override    { return identifier; }
            bool pluginNeedsRescanning (const juce::PluginDescription&) override    { return false; }
            bool doesPluginStillExist (const juce::PluginDescription& d) override    { return fileMightContainThisPluginType (d.fileOrIdentifier); }
            bool canScanForPlugins() const override    { return false; }
            bool isTrivialToScan() const override      { return true; }
            juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override    { return {}; }
            juce::FileSearchPath getDefaultLocationsToSearch() override    { return {}; }
            bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override    { return false; }

        private:
            void createPluginInstance (const juce::PluginDescription& description, double sampleRate, int blockSize,
                                       PluginCreationCallback callback) override
            {
                if (description.fileOrIdentifier != synth().fileOrIdentifier && description.fileOrIdentifier != gain().fileOrIdentifier)
                {
                    callback (nullptr, "There is no test plugin called " + description.fileOrIdentifier);
                    return;
                }

                auto plugin = std::make_unique<TestPlugin> (description.fileOrIdentifier == synth().fileOrIdentifier);
                plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
                callback (std::move (plugin), {});
            }
        };
    }

    std::unique_ptr<juce::AudioPluginFormat> createFormat()    { return std::make_unique<TestFormat>(); }
    juce::PluginDescription synth()                             { return describe ("Test Synth", true); }
    juce::PluginDescription gain()                              { return describe ("Test Gain", false); }
}
