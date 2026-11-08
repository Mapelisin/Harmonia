#include "audio/PlaybackClock.h"

namespace harmonia::audio
{
    namespace
    {
        // Callbacks don't arrive exactly on time; a slow average removes that jitter.
        constexpr double smoothing = 0.02;

        // A bigger jump means the stream restarted (device change): start over.
        constexpr double resyncThresholdSeconds = 0.25;
    }

    PlaybackClock::PlaybackClock (te::Edit& e)
        : edit (e), deviceManager (e.engine.getDeviceManager().deviceManager)
    {
        // Added after the engine's own callback, so the engine's stream time is already
        // advanced past the block when this one runs.
        deviceManager.addAudioCallback (this);
    }

    PlaybackClock::~PlaybackClock()
    {
        deviceManager.removeAudioCallback (this);
    }

    bool PlaybackClock::isPlaying() const
    {
        return edit.getTransport().isPlaying();
    }

    double PlaybackClock::getLatencySeconds() const
    {
        auto latency = edit.engine.getDeviceManager().getOutputLatencySeconds();

        if (auto* context = edit.getTransport().getCurrentPlaybackContext())
            if (context->getSampleRate() > 0.0)
                latency += context->getLatencySamples() / context->getSampleRate();

        return latency;
    }

    std::optional<double> PlaybackClock::getHeardBeat (double wallMs) const
    {
        auto& transport = edit.getTransport();
        auto* context = transport.getCurrentPlaybackContext();

        if (context == nullptr || ! transport.isPlaying())
            return {};

        const auto rendered = synced ? context->globalStreamTimeToEditTime (wallMs * 0.001 - wallMinusStream).inSeconds()
                                     : transport.getPosition().inSeconds();

        std::optional<juce::Range<double>> loop;

        if (transport.looping)
        {
            const auto range = transport.getLoopRange();
            loop = juce::Range<double> (range.getStart().inSeconds(), range.getEnd().inSeconds());
        }

        const auto heard = toHeardSeconds (rendered, getLatencySeconds(), loop);
        return edit.tempoSequence.toBeats (te::TimePosition::fromSeconds (heard)).inBeats();
    }

    juce::Range<double> PlaybackClock::getLoopBeats() const
    {
        auto& transport = edit.getTransport();

        if (! transport.looping)
            return {};

        const auto range = transport.getLoopRange();
        return { edit.tempoSequence.toBeats (range.getStart()).inBeats(),
                 edit.tempoSequence.toBeats (range.getEnd()).inBeats() };
    }

    double PlaybackClock::toHeardSeconds (double rendered, double latency, std::optional<juce::Range<double>> loop)
    {
        auto heard = rendered - latency;

        if (loop.has_value() && loop->getLength() > 0.0)
        {
            // Rendering has wrapped to the loop start, the speakers still play the loop's end.
            if (heard < loop->getStart() && rendered >= loop->getStart())
                heard += loop->getLength();

            heard = juce::jlimit (loop->getStart(), loop->getEnd(), heard);
        }

        return std::max (0.0, heard);
    }

    //==============================================================================
    void PlaybackClock::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                          int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        for (int i = 0; i < numOutputs; ++i)
            if (outputs[i] != nullptr)
                juce::FloatVectorOperations::clear (outputs[i], numSamples);

        const auto rate = sampleRate.load();

        if (rate <= 0.0)
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto blockStart = edit.engine.getDeviceManager().getCurrentStreamTime() - numSamples / rate;
        const auto measured = now - blockStart;
        const auto current = wallMinusStream.load();

        if (! synced || std::abs (measured - current) > resyncThresholdSeconds)
        {
            wallMinusStream = measured;
            synced = true;
        }
        else
        {
            wallMinusStream = current + (measured - current) * smoothing;
        }
    }

    void PlaybackClock::audioDeviceAboutToStart (juce::AudioIODevice* device)
    {
        synced = false;
        sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    }

    void PlaybackClock::audioDeviceStopped()
    {
        synced = false;
        sampleRate = 0.0;
    }
}
