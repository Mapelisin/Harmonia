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
          this from fighting the engine forever.
        - Play records audio instead when `recordsAudio` says so (the armed audio tracks, see
          audio::AudioInputs): straight on from where the song is, without looping (the loop comes
          back at Stop). Stop turns the recordings into clips as one undo step.
        - With setRecordsTakes on and a loop set, it records round the loop instead, from the
          loop's start: each time round is a take of the one clip made at Stop (core::takes).
        - With setPunch on and a loop set, a recording takes only what is played inside the loop
          (punch in / out): the song plays into the loop from a bar before it and on past it. */
    class Transport : private juce::Timer
    {
    public:
        explicit Transport (te::Edit&);
        ~Transport() override;

        void play();
        void stop();
        void togglePlayStop();

        /** Asked at Play: record the armed audio inputs this time? */
        std::function<bool()> recordsAudio;

        /** Recording with a loop set goes round it, a take each time (else straight on). */
        void setRecordsTakes (bool);
        bool recordsTakes() const noexcept          { return takesWanted; }

        /** True while a recording goes round the loop, making takes. */
        bool isRecordingTakes() const noexcept      { return recordRequested && recordingTakes; }

        /** Which time round the loop a recording of takes is (1, 2...); 0 when it isn't one. */
        int getTakeNumber() const;

        /** Punch in / out: a recording with a loop set takes only what is played inside the loop.
            Play then starts a bar before the loop (with a count-in: the count-in plays the song
            in from before the loop, clicking) and the song plays on past the loop until Stop.
            What an armed audio track had inside the loop is replaced by the recording, and isn't
            heard while it is being recorded over. Takes round the loop are off meanwhile.
            The way in is the same when only notes are recorded (`recordsNotes`); which notes
            are kept is up to who records them (getPunchBeats). */
        void setPunch (bool);
        bool isPunchOn() const noexcept             { return punchWanted; }

        /** Asked at Play: will played notes be recorded this time? */
        std::function<bool()> recordsNotes;

        /** True from a Play that punches until Stop. */
        bool isPunching() const noexcept            { return punching; }

        /** The beats a punch records, while one is going. */
        std::optional<juce::Range<double>> getPunchBeats() const;

        /** True from a Play that records audio until Stop. */
        bool isRecordRequested() const noexcept     { return recordRequested; }

        /** True while the engine is actually recording. */
        bool isRecording() const;

        /** Song position (seconds) where the current or last recording started. */
        double getRecordStartSeconds() const noexcept   { return recordStartSeconds; }

        /** Bars of metronome clicks before a recording of audio starts (the song plays them in from
            further back; the recording itself starts where Play was pressed). A setting of the app,
            not of the project. */
        enum class CountIn { off, oneBar, twoBars };
        void setCountIn (CountIn);
        CountIn getCountIn() const;

        /** True while a count-in is playing: recording, but not yet where the recording starts. */
        bool isCountingIn() const;

        /** True from Play until Stop, including while waiting for the engine or the device. */
        bool isPlayRequested() const noexcept       { return playRequested; }

        /** True when the engine is actually playing. */
        bool isPlaying() const;

        /** How often playback had to be resumed since the last Play (diagnostics). */
        int getResumeCount() const noexcept         { return resumeCount; }

        double getPositionSeconds() const;
        void setPositionSeconds (double);

        /** Loops the given beat range; an empty range turns looping off. (While recording audio
            the loop waits until Stop.) */
        void setLoopBeats (double startBeat, double endBeat);

    private:
        te::Edit& edit;
        bool playRequested = false, recordRequested = false, loopWanted = false;
        bool takesWanted = false, recordingTakes = false;
        bool punchWanted = false, punching = false;
        mutable bool countInPending = false, countInStarted = false;
        double recordStartSeconds = 0.0;
        int resumeCount = 0;
        std::vector<double> recentResumeTimesMs;

        te::TransportControl& transport() const    { return edit.getTransport(); }
        bool canPlayNow() const;
        void issuePlay();
        void setEnginePunch (bool);
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (Transport)
    };
}
