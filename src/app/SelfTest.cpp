#include "app/SelfTest.h"
#include "audio/DeviceSetup.h"
#include "audio/DeviceStatus.h"
#include "audio/Instruments.h"
#include "audio/NotePreview.h"
#include "audio/PluginScanner.h"
#include "audio/Transport.h"
#include "core/NoteEditor.h"
#include "core/Project.h"

namespace harmonia::app
{
    namespace
    {
        constexpr double beatsPerBar = 4.0;

        // Am - F - C - G: bass note + triad, one bar each.
        constexpr int progression[4][4] = {
            { 45, 57, 60, 64 },
            { 41, 57, 60, 65 },
            { 48, 55, 60, 64 },
            { 43, 55, 59, 62 },
        };

        constexpr double songBeats = std::size (progression) * beatsPerBar;

        juce::String argumentValue (const juce::StringArray& args, const juce::String& name)
        {
            for (const auto& quoted : args)
                if (const auto arg = quoted.unquoted(); arg.startsWith (name + "="))
                    return arg.fromFirstOccurrenceOf ("=", false, false).unquoted();

            return {};
        }

        //==============================================================================
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
                failures += ok ? 0 : 1;
            }

            int getFailures() const noexcept     { return failures; }

        private:
            juce::File file;
            juce::String text;
            int failures = 0;
        };

        /** Collects JUCE/Tracktion assertion failures (only reported in Debug builds,
            see JUCE_LOG_ASSERTIONS in cmake/HarmoniaJuce.cmake). */
        struct AssertionCollector : juce::Logger
        {
            void logMessage (const juce::String& message) override
            {
                if (message.startsWith ("JUCE Assertion failure"))
                {
                    const juce::ScopedLock sl (lock);
                    failures.addIfNotAlreadyThere (message);
                }
            }

            juce::StringArray take()
            {
                const juce::ScopedLock sl (lock);
                return std::exchange (failures, {});
            }

            juce::CriticalSection lock;
            juce::StringArray failures;
        };

        /** Loudest level seen at a track's level meter. */
        class TrackLevel
        {
        public:
            explicit TrackLevel (te::AudioTrack& t) : track (t)
            {
                if (auto* meter = track.getLevelMeterPlugin())
                    meter->measurer.addClient (client);
            }

            ~TrackLevel()
            {
                if (auto* meter = track.getLevelMeterPlugin())
                    meter->measurer.removeClient (client);
            }

            void poll()
            {
                for (int channel = 0; channel < 2; ++channel)
                    peakDb = std::max (peakDb, client.getAndClearAudioLevel (channel).dB);
            }

            float takePeakDb()
            {
                poll();
                return std::exchange (peakDb, -100.0f);
            }

        private:
            te::AudioTrack& track;
            te::LevelMeasurer::Client client;
            float peakDb = -100.0f;
        };

        /** Runs the message loop for `ms`, polling the level and logging play-state changes. */
        void pump (int ms, TrackLevel& level, audio::Transport& transport, Report& report)
        {
            const auto end = juce::Time::getMillisecondCounterHiRes() + ms;
            bool wasPlaying = transport.isPlaying();

            while (juce::Time::getMillisecondCounterHiRes() < end)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                level.poll();

                if (transport.isPlaying() != wasPlaying)
                {
                    wasPlaying = transport.isPlaying();
                    report.line ("       engine " + juce::String (wasPlaying ? "started" : "stopped") + " at "
                                 + juce::String (transport.getPositionSeconds(), 3) + " s");
                }
            }
        }

        void buildSong (core::Project& project, te::AudioTrack& track)
        {
            auto* clip = project.addMidiClip (track, 0.0, songBeats, "Chords");
            core::NoteEditor editor (*clip);
            std::vector<core::Note> notes;

            for (int bar = 0; bar < (int) std::size (progression); ++bar)
            {
                for (auto pitch : progression[bar])
                {
                    core::Note n;
                    n.pitch = pitch;
                    n.startBeat = bar * beatsPerBar;
                    n.lengthBeats = beatsPerBar - 0.1;
                    n.velocity = 70;
                    notes.push_back (n);
                }
            }

            editor.add (notes);
        }

        struct AudioFileStats
        {
            double seconds = 0.0;
            float peak = 0.0f;
        };

        AudioFileStats analyse (const juce::File& file)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr)
                return {};

            const auto numSamples = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> buffer ((int) reader->numChannels, numSamples);
            reader->read (&buffer, 0, numSamples, 0, true, true);

            return { numSamples / reader->sampleRate, buffer.getMagnitude (0, numSamples) };
        }

        //==============================================================================
        class SelfTest
        {
        public:
            SelfTest (core::Session& s, const SelfTestOptions& o)
                : session (s), options (o), report (o.logFile)
            {
            }

            int run()
            {
                AssertionCollector assertions;
                juce::Logger::setCurrentLogger (&assertions);
                runChecks();
                juce::Logger::setCurrentLogger (nullptr);

                for (const auto& failure : assertions.take())
                    report.check (false, failure);

                report.line (report.getFailures() == 0 ? "RESULT: ALL CHECKS PASSED"
                                                        : "RESULT: " + juce::String (report.getFailures()) + " CHECK(S) FAILED");
                return report.getFailures();
            }

        private:
            void runChecks()
            {
                report.line ("Harmonia self-test, " + juce::Time::getCurrentTime().toString (true, true));
                report.line (juce::SystemStats::getJUCEVersion() + ", " + juce::SystemStats::getOperatingSystemName()
                             + ", " + juce::SystemStats::getCpuModel());

                const auto deviceProblem = audio::checkStartupDevice (session);
                report.line ("Audio device: " + audio::getDeviceStatus (session).toDisplayString());

                if (deviceProblem.isNotEmpty())
                    report.line ("       note: " + deviceProblem);

                auto project = core::Project::createEmpty (session);
                auto& track = *project->getTracks().getFirst();

                // Same order as the app: the transport exists as soon as the project is open.
                const auto xrunsBeforeOpen = xrunCount();
                audio::Transport transport (project->getEdit());
                TrackLevel level (track);

                if (options.useDevice)
                {
                    // Preparing the playback graph the first time briefly blocks the device. That's
                    // why it happens on open, while nothing is playing (a dropout in silence is inaudible).
                    pump (1000, level, transport, report);
                    report.line ("       dropouts while opening the project (silent): "
                                 + juce::String (xrunCount() - xrunsBeforeOpen));
                }

                report.check (audio::instruments::useBuiltInSynth (track).isEmpty(), "Built-in synth on track 1");
                buildSong (*project, track);
                transport.setLoopBeats (0.0, songBeats);

                if (options.useDevice)
                {
                    checkLivePlayback (transport, level);
                    checkPlayAfterDeviceRestart (transport, level);
                }

                checkRender (*project, "builtin-synth.wav");

                if (options.pluginPath.isNotEmpty())
                    checkExternalInstrument (*project, track, transport, level);

                if (options.useDevice)
                    checkNotePreview (track, transport, level);

                transport.stop();
                pump (300, level, transport, report);
            }

            core::Session& session;
            SelfTestOptions options;
            Report report;

            int xrunCount() const
            {
                auto* device = audio::getDeviceManager (session).getCurrentAudioDevice();
                return device != nullptr ? device->getXRunCount() : 0;
            }

            void checkLivePlayback (audio::Transport& transport, TrackLevel& level)
            {
                auto* device = audio::getDeviceManager (session).getCurrentAudioDevice();
                report.check (device != nullptr, "An audio device is open");

                if (device == nullptr)
                    return;

                const auto xrunsBefore = device->getXRunCount();

                // Deliberately no waiting after building the song: play must not get lost.
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                transport.play();
                const auto playCallMs = juce::Time::getMillisecondCounterHiRes() - t0;
                pump (300, level, transport, report);
                report.line ("       play() took " + juce::String (playCallMs, 1) + " ms; xruns after 0.3 s: "
                             + juce::String (device->getXRunCount() - xrunsBefore)
                             + "; resumed " + juce::String (transport.getResumeCount()) + "x");
                pump (5700, level, transport, report);   // long enough to include the engine's MIDI device rescans

                const auto position = transport.getPositionSeconds();
                report.check (transport.isPlaying() && position > 4.0,
                              "Playback started right after building the song and kept going (position "
                                  + juce::String (position, 2) + " s, resumed after engine stops: "
                                  + juce::String (transport.getResumeCount()) + "x)");
                report.check (level.takePeakDb() > -60.0f, "The track is audible while playing");

                const auto xruns = device->getXRunCount() - xrunsBefore;

                if (xrunsBefore >= 0)
                    report.check (xruns == 0, "No audio dropouts (" + juce::String (xruns) + ")");
            }

            /** What happens when the user changes the audio settings and presses Play right away. */
            void checkPlayAfterDeviceRestart (audio::Transport& transport, TrackLevel& level)
            {
                auto& deviceManager = audio::getDeviceManager (session);
                auto* device = deviceManager.getCurrentAudioDevice();

                if (device == nullptr)
                    return;

                transport.stop();
                pump (300, level, transport, report);

                const auto original = deviceManager.getAudioDeviceSetup();
                auto changed = original;

                for (auto size : device->getAvailableBufferSizes())
                    if (size != original.bufferSize && size >= 256)
                        changed.bufferSize = size;

                const auto error = deviceManager.setAudioDeviceSetup (changed, true);
                transport.setPositionSeconds (0.0);
                transport.play();
                pump (2000, level, transport, report);

                const auto position = transport.getPositionSeconds();
                report.check (error.isEmpty() && transport.isPlaying() && position > 1.0,
                              "Play right after the audio device restarted (buffer " + juce::String (original.bufferSize)
                                  + " -> " + juce::String (changed.bufferSize) + ", position " + juce::String (position, 2)
                                  + " s, resumed " + juce::String (transport.getResumeCount()) + "x) " + error);

                transport.stop();
                deviceManager.setAudioDeviceSetup (original, true);
                pump (500, level, transport, report);
            }

            void checkRender (core::Project& project, const juce::String& fileName)
            {
                const auto file = options.renderDirectory.getChildFile (fileName);
                file.deleteFile();

                const auto ok = te::Renderer::renderToFile (project.getEdit(), file, false) && file.existsAsFile();
                report.check (ok, "Offline render -> " + file.getFullPathName());

                if (! ok)
                    return;

                const auto stats = analyse (file);
                const auto expected = project.getEdit().tempoSequence.toTime (te::BeatPosition::fromBeats (songBeats)).inSeconds();

                report.check (stats.peak > 0.01f, "Rendered audio is not silent (peak "
                                                      + juce::String (juce::Decibels::gainToDecibels (stats.peak), 1) + " dBFS)");
                report.check (stats.seconds >= expected - 0.1, "Rendered length " + juce::String (stats.seconds, 2)
                                                                   + " s covers the song (" + juce::String (expected, 2) + " s)");
            }

            void checkExternalInstrument (core::Project& project, te::AudioTrack& track,
                                          audio::Transport& transport, TrackLevel& level)
            {
                audio::PluginScanner scanner (session);
                const auto found = scanner.scanFile (options.pluginPath);
                report.check (! found.isEmpty(), "Scanned " + options.pluginPath + " (" + juce::String (found.size())
                                                     + " plugin(s), out of process: "
                                                     + (session.getEngine().getPluginManager().usesSeparateProcessForScanning() ? "yes" : "no") + ")");

                const auto instrument = std::find_if (found.begin(), found.end(), [] (const auto& d) { return d.isInstrument; });

                if (instrument == found.end())
                    return;

                if (options.useDevice)
                {
                    transport.play();
                    pump (1000, level, transport, report);
                }

                // Switch while playing.
                const auto xrunsBeforeSwitch = xrunCount();
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const auto error = audio::instruments::useExternal (track, *instrument);
                report.line ("       loading '" + instrument->name + "' took "
                             + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
                report.check (error.isEmpty(), "Switched to '" + instrument->name + "' while playing " + error);

                if (error.isNotEmpty())
                    return;

                if (options.useDevice)
                {
                    level.takePeakDb();
                    const auto before = transport.getPositionSeconds();
                    pump (2500, level, transport, report);

                    report.check (transport.isPlaying() && transport.getPositionSeconds() != before,
                                  "Playback continued after the switch");
                    report.line ("       xruns during the switch: " + juce::String (xrunCount() - xrunsBeforeSwitch));
                    report.check (level.takePeakDb() > -60.0f, "'" + instrument->name + "' is audible");
                }

                checkPluginWindow (track);
                checkRender (project, juce::File::createLegalFileName (instrument->name) + ".wav");

                // And back again, still playing.
                if (options.useDevice)
                {
                    transport.play();
                    pump (500, level, transport, report);
                }

                const auto xrunsBeforeSwitchBack = xrunCount();

                report.check (audio::instruments::useBuiltInSynth (track).isEmpty()
                                  && audio::instruments::getName (track) != instrument->name,
                              "Switched back to the built-in synth while playing");

                if (options.useDevice)
                {
                    level.takePeakDb();
                    pump (2000, level, transport, report);
                    report.check (transport.isPlaying(), "Playback continued after switching back");
                    report.line ("       xruns during the switch back: " + juce::String (xrunCount() - xrunsBeforeSwitchBack));
                    report.check (level.takePeakDb() > -60.0f, "Built-in synth is audible again");
                }
            }

            void checkPluginWindow (te::AudioTrack& track)
            {
                auto* plugin = audio::instruments::get (track);

                if (plugin == nullptr)
                    return;

                plugin->showWindowExplicitly();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);

                report.check (plugin->windowState->pluginWindow != nullptr,
                              "Editor window opened for '" + plugin->getName() + "'");

                plugin->windowState->closeWindowExplicitly();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            }

            void checkNotePreview (te::AudioTrack& track, audio::Transport& transport, TrackLevel& level)
            {
                transport.stop();
                pump (1500, level, transport, report);   // let the song's notes die away
                level.takePeakDb();

                audio::NotePreview preview;
                preview.setTrack (&track);
                pump (300, level, transport, report);

                preview.noteOn (60, 100);
                pump (600, level, transport, report);
                const auto peak = level.takePeakDb();
                preview.noteOff (60);

                report.check (peak > -60.0f, "Note preview is audible with the transport stopped (peak "
                                                 + juce::String (peak, 1) + " dB)");
                pump (300, level, transport, report);
            }
        };
    }

    std::optional<SelfTestOptions> SelfTestOptions::fromCommandLine (const juce::String& commandLine)
    {
        juce::StringArray args;
        args.addTokens (commandLine, true);

        for (auto& arg : args)
            arg = arg.unquoted();

        if (! args.contains ("--selftest"))
            return std::nullopt;

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("HarmoniaSelfTest");

        SelfTestOptions options;
        options.logFile = folder.getChildFile ("selftest.log");
        options.renderDirectory = folder;

        if (const auto log = argumentValue (args, "--log"); log.isNotEmpty())
            options.logFile = juce::File::getCurrentWorkingDirectory().getChildFile (log);

        options.pluginPath = argumentValue (args, "--plugin");
        options.useDevice = ! args.contains ("--no-device");
        return options;
    }

    int runSelfTest (core::Session& session, const SelfTestOptions& options)
    {
        return SelfTest (session, options).run();
    }
}
