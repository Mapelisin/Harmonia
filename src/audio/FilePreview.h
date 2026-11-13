#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Plays a sound file once, outside the song: what the sound browser lets you hear before
        the sound goes into the song. A MIDI file plays with a simple built-in sound (its first
        minute). A loop can be heard at the song's tempo, as it will sound once it is in the song.
        Mixed into the output next to the song (it is its own callback on the audio device).
        Message thread only. */
    class FilePreview final : private juce::ChangeListener
    {
    public:
        explicit FilePreview (core::Session&);
        ~FilePreview() override;

        /** True for files it can play (the sound formats the song can use too, and MIDI files). */
        bool canPlay (const juce::File&) const;

        /** A loop heard at another tempo than its own. */
        struct Tempo
        {
            double own = 0.0;       // the file's
            double heard = 0.0;     // the one it is played at
        };

        /** Plays `file` from its start (stopping what was playing); false if it can't be read.
            With `songBpm`, a loop that knows its tempo (core::audio_tempo::findLoopBpm: the sounds
            that follow the tempo once they are in the song) is played at that tempo, faster or
            slower at its own pitch. Anything else plays as it is. */
        bool play (const juce::File&, std::optional<double> songBpm = {});
        void stop();

        bool isPlaying() const;

        /** The file that is playing; empty when nothing is. */
        juce::File getFile() const                  { return file; }

        /** How long the playing file takes to play, in seconds (0 when nothing plays). */
        double getLengthSeconds() const;

        /** Of a loop playing at the song's tempo: its own tempo and that one. */
        std::optional<Tempo> getTempo() const       { return tempo; }

        /** 0 to 1 (0.8 at first: previews are often loud). */
        void setVolume (float gain)                 { transportSource.setGain (gain); }

        /** Called when a preview starts, is stopped or has played to its end. */
        std::function<void()> onStateChanged;

    private:
        core::Session& session;
        juce::TimeSliceThread readAheadThread { "Harmonia file preview" };
        juce::AudioTransportSource transportSource;
        juce::AudioSourcePlayer player;
        juce::AudioBuffer<float> renderedMidi;                     // what a MIDI file sounds like
        std::unique_ptr<juce::PositionableAudioSource> source;     // the file, or renderedMidi
        juce::File file;
        double lengthSeconds = 0.0;
        std::optional<Tempo> tempo;
        bool attached = false;

        void release();
        std::optional<Tempo> findTempo (const juce::File&, std::optional<double> songBpm) const;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        JUCE_DECLARE_NON_COPYABLE (FilePreview)
    };
}
