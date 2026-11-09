#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

/** What Harmonia and a plugin's process share: the memory the audio goes through and the
    messages they send over the pipe. Both ends are the same program (Harmonia.exe, or the test
    program), so the layout of the shared memory is the same on both sides. */
namespace harmonia::sandbox::protocol
{
    constexpr int maxChannels   = 32;
    constexpr int maxSamples    = 4096;    // a longer block goes through in parts
    constexpr int maxParameters = 16384;   // a plugin's parameters past this one aren't shown
    constexpr int midiCapacity  = 65536;   // bytes of MIDI one block can carry each way

    static_assert (std::atomic<uint32_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free,
                   "the shared memory needs atomics that don't take a lock");

    /** Parameter values one side changes and the other picks up: a bit per parameter marks which. */
    struct ParameterChanges
    {
        void post (int index, float value) noexcept
        {
            values[index].store (value, std::memory_order_relaxed);
            changed[index / 32].fetch_or (1u << (index % 32), std::memory_order_release);
            anyChanged.store (1, std::memory_order_release);
        }

        /** Calls `onChange (index, value)` for each value posted since the last collect. */
        template <typename Callback>
        void collect (Callback&& onChange) noexcept
        {
            if (anyChanged.exchange (0, std::memory_order_acq_rel) == 0)
                return;

            for (int word = 0; word < maxParameters / 32; ++word)
            {
                for (auto bits = changed[word].exchange (0, std::memory_order_acquire); bits != 0; bits &= bits - 1)
                {
                    const auto index = word * 32 + countTrailingZeros (bits);
                    onChange (index, values[index].load (std::memory_order_relaxed));
                }
            }
        }

        void clear() noexcept
        {
            collect ([] (int, float) {});
        }

        std::atomic<uint32_t> anyChanged;
        std::atomic<uint32_t> changed[maxParameters / 32];
        std::atomic<float> values[maxParameters];

    private:
        static int countTrailingZeros (uint32_t bits) noexcept
        {
            int count = 0;

            while ((bits & 1u) == 0)
            {
                bits >>= 1;
                ++count;
            }

            return count;
        }
    };

    /** One block's MIDI: events one after another (sample position, size, bytes). */
    struct Midi
    {
        /** The events of `source` from `start` for `numSamples`, moved to start at 0; those that
            don't fit are left out. */
        void write (const juce::MidiBuffer& source, int start, int numSamples) noexcept;

        /** Adds the events to `destination`, `offset` samples later. */
        void read (juce::MidiBuffer& destination, int offset) const noexcept;

        int32_t numBytes;
        uint8_t data[midiCapacity];
    };

    namespace flags
    {
        constexpr uint32_t bypassed    = 1;   // the plugin's own bypass
        constexpr uint32_t nonRealtime = 2;   // rendering to a file: no hurry
        constexpr uint32_t reset       = 4;   // forget tails and held notes first
    }

    static_assert (std::is_trivially_copyable_v<juce::AudioPlayHead::PositionInfo>);

    /** The shared memory. Harmonia asks for a block by counting `requested` up and raising the
        request signal; the plugin's process counts `done` up to it and raises the done signal. */
    struct Block
    {
        std::atomic<uint32_t> requested;
        std::atomic<uint32_t> done;

        uint32_t flags;
        int32_t numChannels, numSamples;
        int32_t hasPosition;
        juce::AudioPlayHead::PositionInfo position;

        Midi midiIn, midiOut;
        float audio[maxChannels][maxSamples];

        ParameterChanges toPlugin;     // set in Harmonia (automation, its knobs)
        ParameterChanges fromPlugin;   // changed by the plugin (its window, itself)
    };

    juce::String getRequestSignalName (const juce::String& memoryName);
    juce::String getDoneSignalName (const juce::String& memoryName);

    /** The command line Harmonia starts a plugin's process with. */
    constexpr const char* hostArgument = "--harmonia-plugin-host";

    //==============================================================================
    /** Messages over the pipe: each a ValueTree of this type. Requests carry an "id" and are
        answered by a "reply" with the same id. */
    namespace message
    {
        // Harmonia asks
        inline const juce::Identifier load ("load"), prepare ("prepare"), release ("release"),
                                      getState ("getState"), setState ("setState"), getValues ("getValues"),
                                      setProgram ("setProgram"), renameProgram ("renameProgram"),
                                      isLayoutSupported ("isLayoutSupported"), setLayout ("setLayout"),
                                      getText ("getText"), getValueForText ("getValueForText"),
                                      showEditor ("showEditor"), hideEditor ("hideEditor"),
                                      trackProperties ("trackProperties"), quit ("quit");

        // the plugin's process tells
        inline const juce::Identifier changed ("changed"), gesture ("gesture"), editorClosed ("editorClosed"),
                                      log ("log");

        inline const juce::Identifier reply ("reply");
    }

    namespace property
    {
        inline const juce::Identifier id ("id"), ok ("ok"), error ("error"), data ("data"),
                                      description ("description"), sampleRate ("sampleRate"), blockSize ("blockSize"),
                                      latency ("latency"), tail ("tail"), hasEditor ("hasEditor"),
                                      acceptsMidi ("acceptsMidi"), producesMidi ("producesMidi"),
                                      isMidiEffect ("isMidiEffect"), supportsMPE ("supportsMPE"),
                                      program ("program"), index ("index"), name ("name"), text ("text"),
                                      value ("value"), begin ("begin"), colour ("colour"),
                                      x ("x"), y ("y"), width ("width"), height ("height"), toFront ("toFront"),
                                      input ("input"), channels ("channels"), enabled ("enabled"),
                                      label ("label"), defaultValue ("default"), steps ("steps"),
                                      discrete ("discrete"), boolean ("boolean"), automatable ("automatable"),
                                      meta ("meta"), inverted ("inverted"), category ("category"),
                                      valueStrings ("valueStrings"), parameterId ("parameterId"),
                                      stateChanged ("stateChanged");

        inline const juce::Identifier programs ("PROGRAMS"), buses ("BUSES"), bus ("BUS"),
                                      parameters ("PARAMETERS"), parameter ("PARAMETER"), layout ("LAYOUT");
    }

    /** A bus's channels as text (its channel types) and back. */
    juce::String channelsToString (const juce::AudioChannelSet&);
    juce::AudioChannelSet channelsFromString (const juce::String&);

    /** A bus layout as a tree (inputs then outputs, each by its channels) and back. */
    juce::ValueTree layoutToTree (const juce::AudioProcessor::BusesLayout&);
    juce::AudioProcessor::BusesLayout layoutFromTree (const juce::ValueTree&);

    /** A tree as the bytes the pipe carries, and back (an invalid tree if they aren't one). */
    juce::MemoryBlock toBytes (const juce::ValueTree&);
    juce::ValueTree fromBytes (const juce::MemoryBlock&);
}
