#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Plays a sound file once, outside the song: what the sound browser lets you hear before
        the sound goes into the song. Mixed into the output next to the song (it is its own
        callback on the audio device). Message thread only. */
    class FilePreview final : private juce::ChangeListener
    {
    public:
        explicit FilePreview (core::Session&);
        ~FilePreview() override;

        /** True for files it can play (the formats the song can use too). */
        bool canPlay (const juce::File&) const;

        /** Plays `file` from its start (stopping what was playing); false if it can't be read. */
        bool play (const juce::File&);
        void stop();

        bool isPlaying() const;

        /** The file that is playing; empty when nothing is. */
        juce::File getFile() const                  { return file; }

        /** The playing file's length in seconds (0 when nothing plays). */
        double getLengthSeconds() const;

        /** 0 to 1 (0.8 at first: previews are often loud). */
        void setVolume (float gain)                 { transportSource.setGain (gain); }

        /** Called when a preview starts, is stopped or has played to its end. */
        std::function<void()> onStateChanged;

    private:
        core::Session& session;
        juce::TimeSliceThread readAheadThread { "Harmonia file preview" };
        juce::AudioTransportSource transportSource;
        juce::AudioSourcePlayer player;
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        juce::File file;
        double lengthSeconds = 0.0;
        bool attached = false;

        void release();
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        JUCE_DECLARE_NON_COPYABLE (FilePreview)
    };
}
