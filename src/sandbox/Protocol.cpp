#include "sandbox/Protocol.h"

#include <cstring>

namespace harmonia::sandbox::protocol
{
    namespace
    {
        constexpr int eventHeaderSize = (int) (sizeof (int32_t) + sizeof (uint16_t));
    }

    juce::String channelsToString (const juce::AudioChannelSet& set)
    {
        juce::StringArray types;

        for (auto type : set.getChannelTypes())
            types.add (juce::String ((int) type));

        return types.joinIntoString (",");
    }

    juce::AudioChannelSet channelsFromString (const juce::String& text)
    {
        juce::Array<juce::AudioChannelSet::ChannelType> types;

        for (const auto& type : juce::StringArray::fromTokens (text, ",", {}))
            if (type.isNotEmpty())
                types.add ((juce::AudioChannelSet::ChannelType) type.getIntValue());

        return juce::AudioChannelSet::channelSetWithChannels (types);
    }

    void Midi::write (const juce::MidiBuffer& source, int start, int numSamples) noexcept
    {
        int32_t used = 0;

        for (auto it = source.findNextSamplePosition (start); it != source.cend(); ++it)
        {
            const auto event = *it;

            if (event.samplePosition >= start + numSamples)
                break;

            const auto size = event.numBytes;

            if (size <= 0 || size > 0xffff || used + eventHeaderSize + size > midiCapacity)
                continue;

            const int32_t position = event.samplePosition - start;
            const auto size16 = (uint16_t) size;
            std::memcpy (data + used, &position, sizeof (position));
            std::memcpy (data + used + sizeof (position), &size16, sizeof (size16));
            std::memcpy (data + used + eventHeaderSize, event.data, (size_t) size);
            used += eventHeaderSize + size;
        }

        numBytes = used;
    }

    void Midi::read (juce::MidiBuffer& destination, int offset) const noexcept
    {
        for (int32_t at = 0; at + eventHeaderSize <= numBytes;)
        {
            int32_t position = 0;
            uint16_t size = 0;
            std::memcpy (&position, data + at, sizeof (position));
            std::memcpy (&size, data + at + sizeof (position), sizeof (size));

            if (at + eventHeaderSize + size > numBytes)
                break;

            destination.addEvent (data + at + eventHeaderSize, size, position + offset);
            at += eventHeaderSize + size;
        }
    }

    juce::String getRequestSignalName (const juce::String& memoryName)    { return memoryName + "-request"; }
    juce::String getDoneSignalName (const juce::String& memoryName)       { return memoryName + "-done"; }

    juce::ValueTree layoutToTree (const juce::AudioProcessor::BusesLayout& layout)
    {
        juce::ValueTree tree (property::layout);

        auto add = [&tree] (const juce::Array<juce::AudioChannelSet>& buses, bool isInput)
        {
            for (const auto& set : buses)
                tree.appendChild (juce::ValueTree (property::bus, { { property::input, isInput },
                                                                    { property::channels, channelsToString (set) } }), nullptr);
        };

        add (layout.inputBuses, true);
        add (layout.outputBuses, false);
        return tree;
    }

    juce::AudioProcessor::BusesLayout layoutFromTree (const juce::ValueTree& tree)
    {
        juce::AudioProcessor::BusesLayout layout;

        for (const auto& bus : tree)
        {
            auto set = channelsFromString (bus[property::channels].toString());

            if (bus[property::input])
                layout.inputBuses.add (set);
            else
                layout.outputBuses.add (set);
        }

        return layout;
    }

    juce::MemoryBlock toBytes (const juce::ValueTree& tree)
    {
        juce::MemoryOutputStream stream;
        tree.writeToStream (stream);
        return stream.getMemoryBlock();
    }

    juce::ValueTree fromBytes (const juce::MemoryBlock& bytes)
    {
        return juce::ValueTree::readFromData (bytes.getData(), bytes.getSize());
    }
}
