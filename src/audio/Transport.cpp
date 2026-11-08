#include "audio/Transport.h"

namespace harmonia::audio
{
    namespace
    {
        constexpr int checkIntervalMs = 20;

        // More resumes than this within the window means something is really wrong: give up.
        constexpr int maxResumesInWindow = 5;
        constexpr double resumeWindowMs = 3000.0;
    }

    Transport::Transport (te::Edit& e)
        : edit (e)
    {
        transport().ensureContextAllocated();
    }

    Transport::~Transport()
    {
        stopTimer();
    }

    void Transport::play()
    {
        playRequested = true;
        resumeCount = 0;
        recentResumeTimesMs.clear();

        if (canPlayNow())
            issuePlay();

        startTimer (checkIntervalMs);
    }

    void Transport::stop()
    {
        playRequested = false;
        stopTimer();
        transport().stop (false, false);
    }

    void Transport::togglePlayStop()
    {
        if (playRequested || isPlaying())
            stop();
        else
            play();
    }

    bool Transport::isPlaying() const
    {
        return transport().isPlaying();
    }

    double Transport::getPositionSeconds() const
    {
        return transport().getPosition().inSeconds();
    }

    void Transport::setPositionSeconds (double seconds)
    {
        transport().setPosition (te::TimePosition::fromSeconds (std::max (0.0, seconds)));
    }

    void Transport::setLoopBeats (double startBeat, double endBeat)
    {
        if (endBeat <= startBeat)
        {
            transport().looping = false;
            return;
        }

        auto& tempo = edit.tempoSequence;
        transport().setLoopRange ({ tempo.toTime (te::BeatPosition::fromBeats (startBeat)),
                                    tempo.toTime (te::BeatPosition::fromBeats (endBeat)) });
        transport().looping = true;
    }

    bool Transport::canPlayNow() const
    {
        if (edit.isRendering())
            return false;

        auto& deviceManager = edit.engine.getDeviceManager();
        return deviceManager.deviceManager.getCurrentAudioDevice() != nullptr
            && deviceManager.getSampleRate() > 0.0;
    }

    void Transport::issuePlay()
    {
        transport().ensureContextAllocated();
        transport().play (false);
    }

    void Transport::timerCallback()
    {
        if (! playRequested)
        {
            stopTimer();
            return;
        }

        if (isPlaying() || ! canPlayNow())
            return;

        // The engine stopped by itself (or a waiting request can start now).
        const auto now = juce::Time::getMillisecondCounterHiRes();
        std::erase_if (recentResumeTimesMs, [now] (double t) { return now - t > resumeWindowMs; });

        if ((int) recentResumeTimesMs.size() >= maxResumesInWindow)
        {
            playRequested = false;
            stopTimer();
            return;
        }

        recentResumeTimesMs.push_back (now);
        ++resumeCount;
        issuePlay();
    }
}
