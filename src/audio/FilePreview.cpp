#include "audio/FilePreview.h"

#include "audio/MidiRender.h"
#include "core/MidiFiles.h"

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
        if (! f.existsAsFile())
            return false;

        auto& formats = session.getEngine().getAudioFileFormatManager().readFormatManager;
        return core::midi_files::isMidiFile (f) || formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
    }

    bool FilePreview::play (const juce::File& f)
    {
        const auto wasPlaying = source != nullptr;
        release();

        auto sampleRate = 0.0;
        auto readAhead = 0;
        juce::TimeSliceThread* thread = nullptr;

        auto& formats = session.getEngine().getAudioFileFormatManager().readFormatManager;

        if (! canPlay (f))
        {
            // Nothing to play.
        }
        else if (core::midi_files::isMidiFile (f))
        {
            renderedMidi = midi_render::render (f);

            if (renderedMidi.getNumSamples() > 0)
            {
                sampleRate = midi_render::defaultSampleRate;
                lengthSeconds = renderedMidi.getNumSamples() / sampleRate;
                source = std::make_unique<juce::MemoryAudioSource> (renderedMidi, false);
            }
        }
        else if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
                 reader != nullptr && reader->lengthInSamples > 0)
        {
            sampleRate = reader->sampleRate;
            lengthSeconds = sampleRate > 0.0 ? (double) reader->lengthInSamples / sampleRate : 0.0;
            source = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
            readAhead = readAheadSamples;
            thread = &readAheadThread;
        }

        if (source == nullptr)
        {
            renderedMidi = {};

            if (wasPlaying && onStateChanged != nullptr)
                onStateChanged();

            return false;
        }

        transportSource.setSource (source.get(), readAhead, thread, sampleRate, 2);
        transportSource.setPosition (0.0);
        transportSource.start();
        file = f;

        if (onStateChanged != nullptr)
            onStateChanged();

        return true;
    }

    void FilePreview::stop()
    {
        if (source == nullptr)
            return;

        release();

        if (onStateChanged != nullptr)
            onStateChanged();
    }

    bool FilePreview::isPlaying() const
    {
        return source != nullptr && transportSource.isPlaying();
    }

    double FilePreview::getLengthSeconds() const
    {
        return source != nullptr ? lengthSeconds : 0.0;
    }

    void FilePreview::release()
    {
        transportSource.stop();
        transportSource.setSource (nullptr);
        source.reset();
        renderedMidi = {};
        file = juce::File();
        lengthSeconds = 0.0;
    }

    void FilePreview::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // Played to its end.
        if (source != nullptr && ! transportSource.isPlaying())
            stop();
    }
}
