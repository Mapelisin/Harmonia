#include "audio/Transport.h"
#include "core/AudioClips.h"
#include "core/Takes.h"

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
        recordRequested = recordsAudio != nullptr && recordsAudio();
        resumeCount = 0;
        recentResumeTimesMs.clear();

        countInPending = recordRequested && edit.getNumCountInBeats() > 0;
        countInStarted = false;

        const auto recordsAnything = recordRequested || (recordsNotes != nullptr && recordsNotes());
        punching = punchWanted && loopWanted && recordsAnything;
        setEnginePunch (punching);

        recordingTakes = recordRequested && takesWanted && loopWanted && ! punching;

        if (punching)
        {
            const auto loop = transport().getLoopRange();
            auto start = loop.getStart();

            // A count-in plays the song in from before where it is; without one, a bar earlier.
            if (! countInPending)
            {
                auto& tempo = edit.tempoSequence;
                const auto loopStartBeat = tempo.toBeats (loop.getStart()).inBeats();
                const auto beatsPerBar = (double) tempo.getTimeSig (0)->numerator.get();
                start = tempo.toTime (te::BeatPosition::fromBeats (std::max (0.0, loopStartBeat - beatsPerBar)));
            }

            transport().setPosition (start);
            recordStartSeconds = loop.getStart().inSeconds();
            transport().looping = false;
        }
        else if (recordingTakes)
        {
            // Every take is one time round, so the first starts where the loop does.
            transport().setPosition (transport().getLoopRange().getStart());
            recordStartSeconds = getPositionSeconds();
            transport().looping = true;
        }
        else if (recordRequested)
        {
            recordStartSeconds = getPositionSeconds();
            transport().looping = false;
        }

        if (canPlayNow())
            issuePlay();

        startTimer (checkIntervalMs);
    }

    void Transport::stop()
    {
        const auto wasRecording = recordRequested || isRecording();
        const auto wasPunching = punching;
        playRequested = recordRequested = punching = false;
        stopTimer();

        // The engine makes the recordings into clips now: one undo step for all of them.
        if (wasRecording)
            edit.getUndoManager().beginNewTransaction ("Record audio");

        transport().stop (false, false);

        if (wasRecording)
        {
            if (recordingTakes)
                core::takes::adoptRecorded (edit);

            // What was recorded plays the way every audio clip does (crossfades where they overlap).
            for (auto* track : te::getAudioTracks (edit))
                for (auto* clip : track->getClips())
                    if (auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip))
                        core::audio_clips::prepareForPlayback (*audioClip);

            recordingTakes = false;
        }

        if (wasPunching)
            setEnginePunch (false);

        if (wasRecording || wasPunching)
            transport().looping = loopWanted;
    }

    void Transport::setPunch (bool shouldPunch)
    {
        punchWanted = shouldPunch;
    }

    std::optional<juce::Range<double>> Transport::getPunchBeats() const
    {
        if (! punching)
            return std::nullopt;

        auto& tempo = edit.tempoSequence;
        const auto loop = transport().getLoopRange();
        return juce::Range<double> (tempo.toBeats (loop.getStart()).inBeats(), tempo.toBeats (loop.getEnd()).inBeats());
    }

    void Transport::setEnginePunch (bool on)
    {
        // The engine punches whenever this is set and it records (between the loop's ends).
        if (edit.recordingPunchInOut.get() != on)
            edit.recordingPunchInOut = on;

        // Replacing: the engine keeps what the track had in the loop silent while it records over
        // it, and cuts it away when the recording becomes a clip (the same undo step).
        const auto mode = te::WaveInputDevice::getMergeModes()[on ? 1 : 0];

        for (auto* input : edit.engine.getDeviceManager().getWaveInputDevices())
            if (input->getMergeMode() != mode)
                input->setMergeMode (mode);
    }

    void Transport::setRecordsTakes (bool shouldRecordTakes)
    {
        takesWanted = shouldRecordTakes;
    }

    int Transport::getTakeNumber() const
    {
        auto* context = transport().getCurrentPlaybackContext();
        const auto loopLength = transport().getLoopRange().getLength().inSeconds();

        if (! isRecordingTakes() || context == nullptr || loopLength <= 0.0 || isCountingIn())
            return 0;

        const auto sinceStart = context->getUnloopedPosition().inSeconds() - recordStartSeconds;
        return 1 + std::max (0, (int) std::floor (sinceStart / loopLength));
    }

    bool Transport::isRecording() const
    {
        return transport().isRecording();
    }

    void Transport::setCountIn (CountIn countIn)
    {
        edit.setCountInMode (countIn == CountIn::twoBars ? te::Edit::CountIn::twoBar
                           : countIn == CountIn::oneBar  ? te::Edit::CountIn::oneBar
                                                         : te::Edit::CountIn::none);
    }

    Transport::CountIn Transport::getCountIn() const
    {
        switch (edit.getCountInMode())
        {
            case te::Edit::CountIn::twoBar:     return CountIn::twoBars;
            case te::Edit::CountIn::oneBar:     return CountIn::oneBar;
            case te::Edit::CountIn::none:
            case te::Edit::CountIn::twoBeat:
            case te::Edit::CountIn::oneBeat:
            default:                            break;
        }

        return CountIn::off;
    }

    bool Transport::isCountingIn() const
    {
        if (! countInPending || ! recordRequested || ! isPlaying())
            return false;

        // The playhead itself (the transport's position follows it only now and then). Until the
        // first block plays, it still says where Play was pressed: that's not the end of the count-in.
        auto* context = transport().getCurrentPlaybackContext();
        const auto position = context != nullptr ? context->getPosition().inSeconds() : getPositionSeconds();

        if (position < recordStartSeconds - 0.001)
        {
            countInStarted = true;
            return true;
        }

        if (countInStarted)
            countInPending = false;

        return countInPending;
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
        loopWanted = endBeat > startBeat;

        if (loopWanted)
        {
            auto& tempo = edit.tempoSequence;
            transport().setLoopRange ({ tempo.toTime (te::BeatPosition::fromBeats (startBeat)),
                                        tempo.toTime (te::BeatPosition::fromBeats (endBeat)) });
        }

        transport().looping = loopWanted && ! punching && (! recordRequested || recordingTakes);
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

        if (recordRequested)
            transport().record (false, false);
        else
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
