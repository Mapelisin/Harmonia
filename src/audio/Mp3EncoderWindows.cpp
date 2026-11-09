#include "audio/Mp3Encoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>

#pragma comment (lib, "mf.lib")
#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfreadwrite.lib")
#pragma comment (lib, "mfuuid.lib")

namespace harmonia::audio::mp3
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        /** COM and Media Foundation on this thread, for as long as it lives. */
        class MediaFoundation
        {
        public:
            MediaFoundation()
            {
                // S_OK or S_FALSE: ours to undo. RPC_E_CHANGED_MODE: the thread has COM its own way (fine).
                comStarted = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
                started = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_NOSOCKET));
            }

            ~MediaFoundation()
            {
                if (started)
                    MFShutdown();

                if (comStarted)
                    CoUninitialize();
            }

            bool isStarted() const noexcept    { return started; }

        private:
            bool comStarted = false, started = false;

            JUCE_DECLARE_NON_COPYABLE (MediaFoundation)
        };

        juce::String describe (HRESULT result)
        {
            return "error 0x" + juce::String::toHexString ((int) result);
        }

        /** The encoder's own output type for these channels, rate and bit rate, or nullptr. */
        ComPtr<IMFMediaType> findOutputType (UINT32 channels, UINT32 sampleRate, int kbps)
        {
            ComPtr<IMFCollection> types;

            if (FAILED (MFTranscodeGetAudioOutputAvailableTypes (MFAudioFormat_MP3, MFT_ENUM_FLAG_ALL, nullptr, &types)))
                return {};

            DWORD count = 0;
            types->GetElementCount (&count);
            const auto bytesPerSecond = (UINT32) kbps * 1000 / 8;

            for (DWORD i = 0; i < count; ++i)
            {
                ComPtr<IUnknown> element;
                ComPtr<IMFMediaType> type;

                if (FAILED (types->GetElement (i, &element)) || FAILED (element.As (&type)))
                    continue;

                UINT32 typeChannels = 0, typeRate = 0, typeBytes = 0;
                type->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &typeChannels);
                type->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &typeRate);
                type->GetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &typeBytes);

                if (typeChannels == channels && typeRate == sampleRate && typeBytes == bytesPerSecond)
                    return type;
            }

            return {};
        }

        /** 16-bit PCM: what the encoder takes. */
        ComPtr<IMFMediaType> makeInputType (UINT32 channels, UINT32 sampleRate)
        {
            ComPtr<IMFMediaType> type;

            if (FAILED (MFCreateMediaType (&type)))
                return {};

            type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            type->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
            type->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, channels);
            type->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
            type->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            type->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
            type->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * channels * 2);
            type->SetUINT32 (MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            return type;
        }

        /** In 100-nanosecond units, as Media Foundation counts time. */
        LONGLONG toMediaTime (juce::int64 samples, UINT32 sampleRate)
        {
            return (LONGLONG) ((double) samples * 10000000.0 / sampleRate + 0.5);
        }

        /** One chunk as 16-bit samples, with a little noise (dither) so quiet endings don't turn grainy. */
        ComPtr<IMFSample> makeSample (const juce::AudioBuffer<float>& buffer, int frames, juce::Random& random)
        {
            const auto channels = buffer.getNumChannels();
            const auto bytes = (DWORD) (frames * channels * 2);

            ComPtr<IMFMediaBuffer> media;
            ComPtr<IMFSample> sample;
            BYTE* data = nullptr;

            if (FAILED (MFCreateMemoryBuffer (bytes, &media)) || FAILED (MFCreateSample (&sample)) || FAILED (media->Lock (&data, nullptr, nullptr)))
                return {};

            auto* out = reinterpret_cast<std::int16_t*> (data);

            for (int i = 0; i < frames; ++i)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto value = buffer.getSample (channel, i) * 32767.0f + (random.nextFloat() - random.nextFloat());
                    out[i * channels + channel] = (std::int16_t) juce::jlimit (-32768, 32767, juce::roundToInt (value));
                }
            }

            media->Unlock();
            media->SetCurrentLength (bytes);
            sample->AddBuffer (media.Get());
            return sample;
        }

    }

    bool isAvailable (int kbps)
    {
        // Asked once: the system's encoders don't change while Harmonia runs.
        static const auto rates = []
        {
            std::map<int, bool> found;
            const MediaFoundation mediaFoundation;

            for (const auto rate : { 128, 160, 192, 224, 256, 320 })
                found[rate] = mediaFoundation.isStarted() && findOutputType (2, 44100, (int) rate) != nullptr;

            return found;
        }();

        const auto found = rates.find (kbps);
        return found != rates.end() && found->second;
    }

    bool canUseSampleRate (double rate)
    {
        return rate == 32000.0 || rate == 44100.0 || rate == 48000.0;
    }

    juce::String encode (const juce::File& wav, const juce::File& mp3, int kbps)
    {
        auto input = wav.createInputStream();

        if (input == nullptr)
            return "Cannot read " + wav.getFullPathName();

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> reader (wavFormat.createReaderFor (input.release(), true));

        if (reader == nullptr)
            return "Cannot read " + wav.getFullPathName();

        const auto channels = (UINT32) reader->numChannels;

        if (channels < 1 || channels > 2)
            return "An MP3 has one or two channels, not " + juce::String ((int) channels);

        if (! canUseSampleRate (reader->sampleRate))
            return "An MP3 can't have a sample rate of " + juce::String (reader->sampleRate) + " Hz";

        if (! isAvailable (kbps))
            return "Windows has no MP3 encoder for " + juce::String (kbps) + " kbps";

        const auto sampleRate = (UINT32) reader->sampleRate;
        const MediaFoundation mediaFoundation;

        if (! mediaFoundation.isStarted())
            return "Windows could not start its MP3 encoder (Media Foundation)";

        auto outputType = findOutputType (channels, sampleRate, kbps);
        auto inputType = makeInputType (channels, sampleRate);

        if (outputType == nullptr || inputType == nullptr)
            return "Windows has no MP3 encoder for " + juce::String (kbps) + " kbps at " + juce::String (sampleRate) + " Hz";

        if (mp3.existsAsFile() && ! mp3.deleteFile())
            return "Cannot overwrite " + mp3.getFullPathName() + " (is it open in another program?)";

        const auto fail = [&mp3] (ComPtr<IMFSinkWriter>& writer, const juce::String& what, HRESULT result)
        {
            writer.Reset();    // lets go of the file
            mp3.deleteFile();
            return "MP3 export failed: " + what + " (" + describe (result) + ")";
        };

        ComPtr<IMFAttributes> attributes;
        ComPtr<IMFSinkWriter> writer;

        if (FAILED (MFCreateAttributes (&attributes, 1)))
            return "MP3 export failed: out of memory";

        attributes->SetGUID (MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MP3);

        if (const auto result = MFCreateSinkWriterFromURL (mp3.getFullPathName().toWideCharPointer(), nullptr, attributes.Get(), &writer);
            FAILED (result))
            return fail (writer, "cannot write " + mp3.getFullPathName(), result);

        DWORD stream = 0;

        if (const auto result = writer->AddStream (outputType.Get(), &stream); FAILED (result))
            return fail (writer, "the encoder refused the format", result);

        if (const auto result = writer->SetInputMediaType (stream, inputType.Get(), nullptr); FAILED (result))
            return fail (writer, "the encoder refused the sound", result);

        if (const auto result = writer->BeginWriting(); FAILED (result))
            return fail (writer, "the encoder did not start", result);

        constexpr int chunkFrames = 8192;
        juce::AudioBuffer<float> buffer ((int) channels, chunkFrames);
        juce::Random random (0x48726d);

        for (juce::int64 position = 0; position < reader->lengthInSamples;)
        {
            const auto frames = (int) juce::jmin ((juce::int64) chunkFrames, reader->lengthInSamples - position);

            if (! reader->read (&buffer, 0, frames, position, true, true))
                return fail (writer, "cannot read " + wav.getFileName(), E_FAIL);

            auto sample = makeSample (buffer, frames, random);

            if (sample == nullptr)
                return fail (writer, "out of memory", E_OUTOFMEMORY);

            const auto start = toMediaTime (position, sampleRate);
            sample->SetSampleTime (start);
            sample->SetSampleDuration (toMediaTime (position + frames, sampleRate) - start);

            if (const auto result = writer->WriteSample (stream, sample.Get()); FAILED (result))
                return fail (writer, "the encoder stopped", result);

            position += frames;
        }

        if (const auto result = writer->Finalize(); FAILED (result))
            return fail (writer, "the file could not be finished", result);

        return {};
    }
}
