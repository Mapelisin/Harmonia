#include "audio/FilePreview.h"

#include "audio/MidiRender.h"
#include "audio/StretchedSource.h"
#include "core/AudioTempo.h"
#include "core/MidiFiles.h"

namespace harmonia::audio
{
    namespace
    {
        constexpr int readAheadSamples = 32768;
        constexpr float previewGain = 0.8f;     // a little under the song: previews are often loud
        constexpr double sameTempo = 0.002;     // a loop this near the song's tempo plays as it is
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

    std::optional<FilePreview::Tempo> FilePreview::findTempo (const juce::File& f, std::optional<double> songBpm) const
    {
        if (! songBpm.has_value() || *songBpm <= 0.0)
            return std::nullopt;

        const auto own = core::audio_tempo::findLoopBpm (session.getEngine(), f, *songBpm);

        if (! own.has_value() || *own <= 0.0)
            return std::nullopt;

        // Not a loop four times as fast or as slow: it wouldn't be the loop any more.
        const auto speed = *songBpm / *own;
        return speed >= StretchedSource::minSpeed && speed <= StretchedSource::maxSpeed ? std::optional<Tempo> (Tempo { *own, *songBpm })
                                                                                     : std::nullopt;
    }

    bool FilePreview::play (const juce::File& f, std::optional<double> songBpm)
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
            const auto numChannels = (int) reader->numChannels;
            auto sound = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
            tempo = findTempo (f, songBpm);

            // A loop at the song's tempo: stretched as it is read ahead (on the read-ahead thread).
            if (const auto speed = tempo.has_value() ? tempo->heard / tempo->own : 1.0;
                sampleRate > 0.0 && std::abs (speed - 1.0) > sameTempo)
            {
                source = std::make_unique<StretchedSource> (std::move (sound), sampleRate, numChannels, speed);
                lengthSeconds /= speed;
            }
            else
            {
                source = std::move (sound);
            }

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
        tempo.reset();
    }

    void FilePreview::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // Played to its end.
        if (source != nullptr && ! transportSource.isPlaying())
            stop();
    }
}
