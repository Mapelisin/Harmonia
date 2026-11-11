#include "audio/TrackDelay.h"

#include "audio/Effects.h"

namespace harmonia::audio::track_delay
{
    namespace
    {
        const juce::Identifier msId ("ms");

        Plugin* find (te::AudioTrack& track)
        {
            for (auto* plugin : track.pluginList)
                if (auto* delay = dynamic_cast<Plugin*> (plugin))
                    return delay;

            return nullptr;
        }

        double tidy (double ms)
        {
            return std::round (juce::jlimit (-maxMs, maxMs, ms) * 10.0) / 10.0;
        }
    }

    double getMs (te::AudioTrack& track)
    {
        auto* delay = find (track);
        return delay != nullptr ? tidy (delay->msValue.get()) : 0.0;
    }

    void setMs (te::AudioTrack& track, double ms)
    {
        ms = tidy (ms);
        auto* delay = find (track);

        if (ms == 0.0)
        {
            // No delay, nothing in the song.
            if (delay != nullptr)
                delay->deleteFromParent();

            return;
        }

        if (delay == nullptr)
        {
            const auto plugin = track.edit.getPluginCache().createNewPlugin (Plugin::xmlTypeName, {});

            if (plugin == nullptr)
                return;

            track.pluginList.insertPlugin (plugin, effects::getFaderIndex (track.pluginList), nullptr);
            delay = find (track);

            if (delay == nullptr)
                return;
        }

        delay->msValue = (float) ms;
    }

    juce::String describe (double ms)
    {
        ms = tidy (ms);
        const auto whole = std::abs (ms - std::round (ms)) < 0.05;
        return (ms > 0.0 ? "+" : "") + (whole ? juce::String (juce::roundToInt (ms)) : juce::String (ms, 1)) + " ms";
    }

    std::optional<double> parse (const juce::String& text)
    {
        const auto number = text.trim().upToFirstOccurrenceOf ("ms", false, true).trim().replaceCharacter (',', '.');

        if (number.isEmpty() || ! number.containsOnly ("+-.0123456789") || ! number.containsAnyOf ("0123456789"))
            return std::nullopt;

        return number.getDoubleValue();
    }

    //==============================================================================
    const char* Plugin::xmlTypeName = "harmoniaTrackDelay";

    Plugin::Plugin (te::PluginCreationInfo info)
        : te::Plugin (info)
    {
        msValue.referTo (state, msId, getUndoManager(), 0.0f);

        // The engine learns of another latency when the song's sound is put together again.
        restartTimer.setCallback ([this]
        {
            restartTimer.stopTimer();
            edit.restartPlayback();
        });

        // As the song has it: nothing to tell the engine yet, it asks when it puts the sound together.
        const auto ms = tidy (msValue.get());
        heldSamples = ms > 0.0 ? juce::roundToInt (ms * sampleRate / 1000.0) : 0;
        reportedLatency = ms < 0.0 ? -ms / 1000.0 : 0.0;
    }

    Plugin::~Plugin()
    {
        notifyListenersOfDeletion();
    }

    void Plugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        te::copyPropertiesToCachedValues (v, msValue);
        update();
    }

    void Plugin::initialise (const te::PluginInitialisationInfo& info)
    {
        sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
        line.setSize (2, juce::roundToInt (sampleRate * maxMs / 1000.0) + 2, false, true);
        position = 0;
        update();
    }

    void Plugin::reset()
    {
        line.clear();
    }

    void Plugin::update()
    {
        const auto ms = tidy (msValue.get());
        heldSamples = ms > 0.0 ? juce::roundToInt (ms * sampleRate / 1000.0) : 0;

        if (const auto latency = ms < 0.0 ? -ms / 1000.0 : 0.0; latency != reportedLatency)
        {
            reportedLatency = latency;
            restartTimer.startTimer (50);
        }
    }

    void Plugin::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
    {
        if (tree == state && id == msId)
        {
            msValue.forceUpdateOfCachedValue();
            update();
        }

        te::Plugin::valueTreePropertyChanged (tree, id);
    }

    double Plugin::getLatencySeconds()
    {
        // Earlier: the track is said to be this slow, and isn't. The engine holds the rest back.
        const auto ms = tidy (msValue.get());
        return ms < 0.0 ? -ms / 1000.0 : 0.0;
    }

    double Plugin::getTailLength() const
    {
        return heldSamples.load() / sampleRate.load();
    }

    void Plugin::applyToBuffer (const te::PluginRenderContext& context)
    {
        const auto length = line.getNumSamples();

        if (context.destBuffer == nullptr || length < 2)
            return;

        auto& buffer = *context.destBuffer;
        const auto numChannels = std::min (line.getNumChannels(), buffer.getNumChannels());
        const auto numSamples = context.bufferNumSamples;
        const auto held = std::min (heldSamples.load (std::memory_order_relaxed), length - 1);

        // Each sample goes into the ring; out comes the one from `held` samples ago (itself, at 0).
        for (int c = 0; c < numChannels; ++c)
        {
            auto* samples = buffer.getWritePointer (c, context.bufferStartSample);
            auto* ring = line.getWritePointer (c);
            auto at = position;

            for (int i = 0; i < numSamples; ++i)
            {
                ring[at] = samples[i];
                const auto from = at - held;
                samples[i] = ring[from < 0 ? from + length : from];

                if (++at == length)
                    at = 0;
            }
        }

        position = (position + numSamples) % length;
    }

    void registerType (te::Engine& engine)
    {
        engine.getPluginManager().createBuiltInType<Plugin>();
    }
}
