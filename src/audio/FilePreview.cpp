#include "audio/FilePreview.h"

namespace harmonia::audio
{
    namespace
    {
        constexpr int readAheadSamples = 32768;
        constexpr float previewGain = 0.8f;     // a little under the song: previews are often loud
    }

    FilePreview::FilePreview (core::Session& s)
        : session (s)
    {
        readAheadThread.startThread (juce::Thread::Priority::normal);
        transportSource.addChangeListener (this);
        transportSource.setGain (previewGain);
        player.setSource (&transportSource);

        if (session.devicesEnabled())
        {
            session.getEngine().getDeviceManager().deviceManager.addAudioCallback (&player);
            attached = true;
        }
    }

    FilePreview::~FilePreview()
    {
        if (attached)
            session.getEngine().getDeviceManager().deviceManager.removeAudioCallback (&player);

        player.setSource (nullptr);
        transportSource.removeChangeListener (this);
        release();
        readAheadThread.stopThread (2000);
    }

    bool FilePreview::canPlay (const juce::File& f) const
    {
        auto& formats = session.getEngine().getAudioFileFormatManager().readFormatManager;
        return f.existsAsFile() && formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
    }

    bool FilePreview::play (const juce::File& f)
    {
        const auto wasPlaying = readerSource != nullptr;
        release();

        std::unique_ptr<juce::AudioFormatReader> reader;

        if (canPlay (f))
            reader.reset (session.getEngine().getAudioFileFormatManager().readFormatManager.createReaderFor (f));

        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            if (wasPlaying && onStateChanged != nullptr)
                onStateChanged();

            return false;
        }

        const auto sampleRate = reader->sampleRate;
        lengthSeconds = sampleRate > 0.0 ? (double) reader->lengthInSamples / sampleRate : 0.0;
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
        transportSource.setSource (readerSource.get(), readAheadSamples, &readAheadThread, sampleRate, 2);
        transportSource.setPosition (0.0);
        transportSource.start();
        file = f;

        if (onStateChanged != nullptr)
            onStateChanged();

        return true;
    }

    void FilePreview::stop()
    {
        if (readerSource == nullptr)
            return;

        release();

        if (onStateChanged != nullptr)
            onStateChanged();
    }

    bool FilePreview::isPlaying() const
    {
        return readerSource != nullptr && transportSource.isPlaying();
    }

    double FilePreview::getLengthSeconds() const
    {
        return readerSource != nullptr ? lengthSeconds : 0.0;
    }

    void FilePreview::release()
    {
        transportSource.stop();
        transportSource.setSource (nullptr);
        readerSource.reset();
        file = juce::File();
        lengthSeconds = 0.0;
    }

    void FilePreview::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // Played to its end.
        if (readerSource != nullptr && ! transportSource.isPlaying())
            stop();
    }
}
