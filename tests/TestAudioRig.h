#pragma once

#include "TestAudioFiles.h"
#include "audio/DrumKit.h"
#include "audio/Limiter.h"
#include "audio/Pump.h"
#include "core/Project.h"
#include "core/Tracks.h"

namespace harmonia::test
{
    /** An engine of its own (so the shared test engine never gets a device) with an "audio
        interface" the test runs: `process` feeds its inputs and measures what comes out, the way a
        sound card would hear the song. */
    struct AudioRig
    {
        explicit AudioRig (int numInputs = 1)
        {
            audio::limiter::registerType (engine());
            audio::pump::registerType (engine());
            audio::drum_kit::registerType (engine());
            params.sampleRate = 44100.0;
            params.blockSize = 512;
            params.inputChannels = numInputs;
            params.outputChannels = 2;
            audioIO.initialise (params);
            audioIO.prepareToPlay (params.sampleRate, params.blockSize);
            engine().getDeviceManager().dispatchPendingUpdates();
        }

        ~AudioRig()
        {
            project.reset();
            engine().getDeviceManager().deviceManager.closeAudioDevice();
            engine().getDeviceManager().removeHostedAudioDeviceInterface();
            engine().getAudioFileManager().releaseAllFiles();
        }

        te::Engine& engine()    { return session.getEngine(); }

        static core::Session::Options makeOptions (const juce::File& recordings)
        {
            core::Session::Options options;
            options.openDevices = false;
            options.settingsName = "HarmoniaRecordingTests";
            options.recordingsFolder = recordings;
            return options;
        }

        /** Runs the engine for `seconds` with a 440 Hz tone at `inputLevel` (or silence) at the
            inputs. Returns the RMS of what came out (the first output channel). */
        float process (double seconds, float inputLevel = 0.5f)
        {
            const auto numSamples = (int) (seconds * params.sampleRate);
            const auto numChannels = std::max (params.inputChannels, params.outputChannels);
            juce::MidiBuffer midi;
            double sumOfSquares = 0.0;

            for (int start = 0; start < numSamples; start += params.blockSize)
            {
                const auto n = std::min (params.blockSize, numSamples - start);
                juce::AudioBuffer<float> block (numChannels, n);
                block.clear();

                for (int channel = 0; channel < params.inputChannels; ++channel)
                    for (int i = 0; i < n; ++i)
                        block.setSample (channel, i, inputLevel * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0
                                                                                    * (samplesProcessed + i) / params.sampleRate));

                audioIO.processBlock (block, midi);
                samplesProcessed += n;

                for (int i = 0; i < n; ++i)
                    sumOfSquares += juce::square ((double) block.getSample (0, i));
            }

            return numSamples > 0 ? (float) std::sqrt (sumOfSquares / numSamples) : 0.0f;
        }

        te::AudioTrack& addAudioTrack (const juce::String& name)
        {
            auto* track = project->addTrack (name);
            core::tracks::setKind (*track, core::tracks::Kind::audio);
            return *track;
        }

        TempFolder temp;
        core::Session session { makeOptions (temp.getFile ("Recordings")) };
        te::HostedAudioDeviceInterface::Parameters params;
        te::HostedAudioDeviceInterface& audioIO { engine().getDeviceManager().getHostedAudioDeviceInterface() };
        std::unique_ptr<core::Project> project = core::Project::createEmpty (session);
        juce::int64 samplesProcessed = 0;
    };
}
