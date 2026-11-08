#include "SelfTest.h"
#include "AudioDevice.h"
#include "EngineSession.h"
#include "PluginScanner.h"

namespace harmonia::spike
{
    namespace
    {
        class Report
        {
        public:
            explicit Report (juce::File f) : file (std::move (f))
            {
                file.getParentDirectory().createDirectory();
                file.replaceWithText ({});
            }

            void line (const juce::String& s)
            {
                text << s << "\n";
                file.replaceWithText (text);
            }

            void check (bool ok, const juce::String& what)
            {
                line ((ok ? "[PASS] " : "[FAIL] ") + what);

                if (! ok)
                    ++failures;
            }

            int getFailures() const noexcept { return failures; }

        private:
            juce::File file;
            juce::String text;
            int failures = 0;
        };

        struct AudioStats
        {
            double seconds = 0.0;
            float peak = 0.0f;
            float rms = 0.0f;
        };

        AudioStats analyse (const juce::File& file)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr)
                return {};

            const auto numSamples = (int) reader->lengthInSamples;
            const auto numChannels = (int) reader->numChannels;

            juce::AudioBuffer<float> buffer (numChannels, numSamples);
            reader->read (&buffer, 0, numSamples, 0, true, true);

            AudioStats stats;
            stats.seconds = numSamples / reader->sampleRate;
            stats.peak = buffer.getMagnitude (0, numSamples);

            for (int ch = 0; ch < numChannels; ++ch)
                stats.rms += buffer.getRMSLevel (ch, 0, numSamples) / (float) numChannels;

            return stats;
        }

        void checkRender (EngineSession& session, const juce::File& file, Report& report)
        {
            const auto label = "'" + session.getInstrumentName() + "'";
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const bool rendered = session.renderToFile (file);
            const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;

            report.check (rendered, "Offline render with " + label + " -> " + file.getFullPathName());

            if (! rendered)
                return;

            const auto stats = analyse (file);
            const auto expected = session.getLoopLengthSeconds();

            report.check (stats.peak > 0.01f, "Rendered audio is not silent (peak "
                                                  + juce::String (juce::Decibels::gainToDecibels (stats.peak), 1)
                                                  + " dBFS, RMS "
                                                  + juce::String (juce::Decibels::gainToDecibels (stats.rms), 1) + " dBFS)");
            report.check (std::abs (stats.seconds - expected) < 1.0,
                          "Rendered length " + juce::String (stats.seconds, 2) + " s matches loop "
                              + juce::String (expected, 2) + " s");
            report.line ("       render took " + juce::String (elapsedMs, 0) + " ms = "
                         + juce::String (expected * 1000.0 / juce::jmax (1.0, elapsedMs), 1) + "x realtime");
        }

        void checkLivePlayback (te::Engine& engine, EngineSession& session, Report& report)
        {
            auto& dm = engine.getDeviceManager();
            auto* device = dm.deviceManager.getCurrentAudioDevice();

            report.check (device != nullptr, "Audio device is open: " + audio_device::describeCurrentDevice (engine));

            if (device == nullptr)
                return;

            const auto xrunsBefore = device->getXRunCount();
            dm.restCPUStatistics();

            auto& transport = session.getEdit().getTransport();
            auto describeState = [&transport]
            {
                auto* epc = transport.getCurrentPlaybackContext();
                return "transport playing=" + juce::String ((int) transport.isPlaying())
                     + " context=" + juce::String ((int) (epc != nullptr))
                     + " contextPlaying=" + juce::String ((int) (epc != nullptr && epc->isPlaying()))
                     + " editLoading=" + juce::String ((int) transport.edit.isLoading());
            };

            session.play();
            report.line ("       after play(): " + describeState());
            float peakCpu = 0.0f;

            for (int i = 0; i < 30; ++i)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                peakCpu = juce::jmax (peakCpu, dm.getCpuUsage());
            }

            report.line ("       after 3 s: " + describeState());
            const auto position = session.getPositionSeconds();
            session.stop();

            report.check (position > 2.0, "Transport advanced during 3 s of live playback with '"
                                              + session.getInstrumentName() + "' (position "
                                              + juce::String (position, 2) + " s)");
            report.line ("       peak CPU load of audio callback: " + juce::String (peakCpu * 100.0f, 1) + " %");

            const auto xrunsAfter = device->getXRunCount();

            if (xrunsBefore >= 0 && xrunsAfter >= 0)
                report.check (xrunsAfter == xrunsBefore, "No audio dropouts (xruns: "
                                                             + juce::String (xrunsAfter - xrunsBefore) + ")");
            else
                report.line ("       driver does not report dropouts");
        }

        void checkPluginWindow (EngineSession& session, Report& report)
        {
            auto* plugin = session.getInstrument();

            if (plugin == nullptr)
                return;

            plugin->showWindowExplicitly();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);

            const bool shown = plugin->windowState->pluginWindow != nullptr;
            report.check (shown, "Plugin editor window opened for '" + plugin->getName() + "'");

            plugin->windowState->closeWindowExplicitly();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        }
    }

    int runSelfTest (te::Engine& engine, const SelfTestOptions& options)
    {
        Report report (options.logFile);

        report.line ("Harmonia spike 01 self-test, " + juce::Time::getCurrentTime().toString (true, true));
        report.line (juce::SystemStats::getJUCEVersion() + ", " + juce::SystemStats::getOperatingSystemName()
                     + ", " + juce::SystemStats::getCpuModel());
        report.line ("Audio devices:\n" + audio_device::listAvailableDevices (engine));

        if (options.testLiveDevice)
        {
            const auto asioError = audio_device::preferAsio (engine);
            report.line (asioError.isEmpty() ? "Using ASIO" : "ASIO not used: " + asioError);
        }

        EngineSession session (engine);
        report.check (session.getInstrument() != nullptr, "Built-in synth created");

        // Finding: play() issued right after the edit is built gets cancelled by the engine's
        // asynchronous graph rebuild. Letting pending async work settle avoids it; the real app
        // needs a proper "play when graph is ready" mechanism (see docs/research/spike-01.md).
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);

        if (options.testLiveDevice)
            checkLivePlayback (engine, session, report);

        checkRender (session, options.renderDirectory.getChildFile ("builtin-synth.wav"), report);

        if (options.pluginPath.isNotEmpty())
        {
            PluginScanner scanner (engine);
            const auto found = scanner.scanFile (options.pluginPath);
            report.check (! found.isEmpty(), "Scanned " + options.pluginPath + " (" + juce::String (found.size()) + " plugin(s))");

            for (auto& desc : found)
            {
                report.line ("       found: " + desc.name + " by " + desc.manufacturerName
                             + (desc.isInstrument ? " [instrument]" : " [effect]"));

                if (! desc.isInstrument)
                    continue;

                const auto error = session.useExternalInstrument (desc);
                report.check (error.isEmpty(), "Loaded instrument '" + desc.name + "' " + error);

                if (error.isNotEmpty())
                    continue;

                juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);

                if (options.testLiveDevice)
                    checkLivePlayback (engine, session, report);

                checkRender (session, options.renderDirectory.getChildFile (juce::File::createLegalFileName (desc.name) + ".wav"), report);

                checkPluginWindow (session, report);
                break;
            }
        }

        report.line (report.getFailures() == 0 ? "RESULT: ALL CHECKS PASSED"
                                                : "RESULT: " + juce::String (report.getFailures()) + " CHECK(S) FAILED");
        return report.getFailures() == 0 ? 0 : 1;
    }
}
