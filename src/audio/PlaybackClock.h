#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Where in the song the listener is, for placing played notes. */
    struct SongClock
    {
        virtual ~SongClock() = default;

        virtual bool isPlaying() const = 0;

        /** The song beat the listener heard at `wallMs` (juce::Time::getMillisecondCounterHiRes()),
            or nothing when the song isn't playing. */
        virtual std::optional<double> getHeardBeat (double wallMs) const = 0;
    };

    /** SongClock of an edit's transport.

        The engine renders audio ahead of what comes out of the speakers: by the output latency
        of the audio interface plus the latency of the plugins (delay compensation). A player
        hears the song that much late and plays along with what they hear, so a note played at a
        given moment belongs at the position the listener heard then, not at the position being
        rendered.

        The rendered position is interpolated between audio callbacks: each callback pairs the
        engine's stream time with the wall clock, so the position is exact to well under a
        millisecond instead of jumping once per audio buffer. */
    class PlaybackClock final : public SongClock,
                                private juce::AudioIODeviceCallback
    {
    public:
        explicit PlaybackClock (te::Edit&);
        ~PlaybackClock() override;

        bool isPlaying() const override;
        std::optional<double> getHeardBeat (double wallMs) const override;

        /** Total delay between rendering and hearing, in seconds. */
        double getLatencySeconds() const;

        /** The heard position for a rendered one: `latency` earlier, wrapped into the loop. */
        static double toHeardSeconds (double renderedSeconds, double latencySeconds,
                                      std::optional<juce::Range<double>> loopSeconds);

    private:
        te::Edit& edit;
        juce::AudioDeviceManager& deviceManager;

        // Wall clock minus engine stream time at the start of the last audio callback, smoothed.
        std::atomic<double> wallMinusStream { 0.0 };
        std::atomic<bool> synced { false };
        std::atomic<double> sampleRate { 0.0 };

        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;

        JUCE_DECLARE_NON_COPYABLE (PlaybackClock)
    };
}
