#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Play / stop / position for one project, on top of Tracktion's TransportControl.

        - The playback graph is prepared when the Transport is created (i.e. when a project
          opens). Preparing it the first time briefly blocks the audio device; doing that at
          Play would cause an audible click.
        - Play means "keep playing until the user presses Stop". Tracktion stops playback by
          itself whenever it rebuilds its device connections: after the audio device restarts,
          when MIDI devices appear (also twice in the first seconds after start-up). Such stops
          are resumed from the same position. If there is no working audio device, the request
          waits and playback starts once a device runs ("play when ready"). A resume limit keeps
          this from fighting the engine forever. */
    class Transport : private juce::Timer
    {
    public:
        explicit Transport (te::Edit&);
        ~Transport() override;

        void play();
        void stop();
        void togglePlayStop();

        /** True from Play until Stop, including while waiting for the engine or the device. */
        bool isPlayRequested() const noexcept       { return playRequested; }

        /** True when the engine is actually playing. */
        bool isPlaying() const;

        /** How often playback had to be resumed since the last Play (diagnostics). */
        int getResumeCount() const noexcept         { return resumeCount; }

        double getPositionSeconds() const;
        void setPositionSeconds (double);

        /** Loops the given beat range; an empty range turns looping off. */
        void setLoopBeats (double startBeat, double endBeat);

    private:
        te::Edit& edit;
        bool playRequested = false;
        int resumeCount = 0;
        std::vector<double> recentResumeTimesMs;

        te::TransportControl& transport() const    { return edit.getTransport(); }
        bool canPlayNow() const;
        void issuePlay();
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (Transport)
    };
}
