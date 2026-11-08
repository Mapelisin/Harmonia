#include "app/SelfTest.h"
#include "audio/AudioInputs.h"
#include "audio/Automation.h"
#include "audio/Sends.h"
#include "audio/DeviceSetup.h"
#include "audio/DeviceStatus.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "audio/Export.h"
#include "audio/NotePreview.h"
#include "audio/PlaybackClock.h"
#include "audio/PluginScanner.h"
#include "audio/Transport.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Tracks.h"

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
                setQuiet (*project, options.quiet);

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
                    checkPlaybackClock (project->getEdit(), transport, level);
                    checkPlayAfterDeviceRestart (transport, level);
                    checkEffects (*project, track, transport, level);
                    checkSends (*project, track, transport, level);
                    checkAutomation (*project, track, transport, level);
                }

                checkAudioClip (*project, transport);

                if (options.useDevice)
                    checkAudioRecording (*project, transport);

                checkRender (*project, "builtin-synth.wav");
                checkExport (*project, "export.wav");

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

            /** The mixer while playing: effects come, move and go without stopping the sound;
                the track and master meters show the song. Leaves no effects behind. */
            void checkEffects (core::Project& project, te::AudioTrack& track, audio::Transport& transport, TrackLevel& level)
            {
                namespace effects = audio::effects;
                auto* device = audio::getDeviceManager (session).getCurrentAudioDevice();

                if (device == nullptr)
                    return;

                auto& edit = project.getEdit();
                transport.setPositionSeconds (0.0);
                transport.play();
                pump (500, level, transport, report);
                const auto xrunsBefore = device->getXRunCount();

                juce::StringArray added, failed;
                juce::String error;

                for (const auto& type : effects::getBuiltIn())
                {
                    if (effects::add (track.pluginList, type, error) != nullptr)
                        added.add (type.name);
                    else
                        failed.add (type.name + " (" + error + ")");
                }

                auto* masterEffect = effects::add (edit.getMasterPluginList(), effects::getBuiltIn()[1], error);

                report.check (failed.isEmpty() && masterEffect != nullptr,
                              "Added effects while playing: " + added.joinIntoString (", ") + " on track 1, "
                                  + (masterEffect != nullptr ? masterEffect->getName() : "nothing") + " on the master"
                                  + (failed.isEmpty() ? juce::String() : "; failed: " + failed.joinIntoString (", ")));

                level.takePeakDb();
                pump (1500, level, transport, report);
                const auto trackPeak = level.takePeakDb();
                report.check (transport.isPlaying() && trackPeak > -60.0f,
                              "Still playing and audible through the effects (track peak "
                                  + juce::String (trackPeak, 1) + " dBFS)");

                // The master meter, as the mixer reads it. It measures after the master volume, which
                // --quiet turns all the way down: then it can only show that it is fed.
                auto masterPeak = -100.0f;
                auto masterChannels = 0;

                if (auto* context = edit.getTransport().getCurrentPlaybackContext())
                {
                    te::LevelMeasurer::Client client;
                    context->masterLevels.addClient (client);
                    const auto end = juce::Time::getMillisecondCounterHiRes() + 800;

                    while (juce::Time::getMillisecondCounterHiRes() < end)
                    {
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

                        for (int channel = 0; channel < 2; ++channel)
                            masterPeak = std::max (masterPeak, client.getAndClearAudioLevel (channel).dB);

                        masterChannels = std::max (masterChannels, client.getNumChannelsUsed());
                    }

                    context->masterLevels.removeClient (client);
                }

                if (options.quiet)
                    report.check (masterChannels > 0, "The master meter is fed (" + juce::String (masterChannels)
                                                          + " channels; silent: --quiet turns the master down)");
                else
                    report.check (masterPeak > -60.0f, "The master meter shows the song (peak " + juce::String (masterPeak, 1) + " dBFS)");

                // Reorder and bypass while playing.
                auto chain = effects::get (track.pluginList);

                if (chain.size() >= 3)
                {
                    effects::move (*chain.getLast(), 0);
                    effects::setBypassed (*chain[1], true);
                }

                pump (800, level, transport, report);
                report.check (transport.isPlaying() && level.takePeakDb() > -60.0f,
                              "Moved and bypassed effects while playing, still audible");

                for (auto* plugin : effects::get (track.pluginList))
                    effects::remove (*plugin);

                if (masterEffect != nullptr)
                    effects::remove (*masterEffect);

                level.takePeakDb();
                pump (800, level, transport, report);
                report.check (effects::get (track.pluginList).isEmpty() && transport.isPlaying() && level.takePeakDb() > -60.0f,
                              "Removed the effects while playing, still audible");

                report.line ("       dropouts while changing effects: " + juce::String (device->getXRunCount() - xrunsBefore));
                transport.stop();
                pump (300, level, transport, report);
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

            static void setQuiet (core::Project& project, bool quiet)
            {
                if (auto master = project.getEdit().getMasterVolumePlugin())
                    master->setVolumeDb (quiet ? -100.0f : 0.0f);
            }

            /** Where played notes land: the clock behind recording must run smoothly with the
                song, without the per-buffer steps of the engine's position, and lag the
                rendered position by the audio latency. */
            void checkPlaybackClock (te::Edit& edit, audio::Transport& transport, TrackLevel& level)
            {
                if (! transport.isPlaying())
                {
                    report.check (false, "Playback clock: the song should be playing");
                    return;
                }

                audio::PlaybackClock clock (edit);
                pump (500, level, transport, report);   // lets the clock settle on the callbacks

                struct Sample { double wall, heard, rendered; };
                std::vector<Sample> samples;
                const auto loopSeconds = edit.tempoSequence.toTime (te::BeatPosition::fromBeats (songBeats)).inSeconds();
                const auto end = juce::Time::getMillisecondCounterHiRes() + 2000.0;

                while (juce::Time::getMillisecondCounterHiRes() < end)
                {
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
                    const auto now = juce::Time::getMillisecondCounterHiRes();

                    if (const auto beat = clock.getHeardBeat (now))
                        samples.push_back ({ now * 0.001,
                                             edit.tempoSequence.toTime (te::BeatPosition::fromBeats (*beat)).inSeconds(),
                                             transport.getPositionSeconds() });
                }

                if (samples.size() < 100)
                {
                    report.check (false, "Playback clock: only " + juce::String ((int) samples.size()) + " readings");
                    return;
                }

                // The song loops: unwrap both positions into straight lines.
                const auto unwrap = [&samples, loopSeconds] (double Sample::* value)
                {
                    auto offset = 0.0, previous = samples.front().*value;

                    for (auto& s : samples)
                    {
                        if (s.*value < previous - loopSeconds * 0.5)
                            offset += loopSeconds;

                        previous = s.*value;
                        s.*value += offset;
                    }
                };

                unwrap (&Sample::heard);
                unwrap (&Sample::rendered);

                // Least-squares line through (wall, position); the jitter is the worst distance from it.
                const auto fit = [&samples] (double Sample::* value)
                {
                    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
                    const auto x0 = samples.front().wall;

                    for (const auto& s : samples)
                    {
                        const auto x = s.wall - x0, y = s.*value;
                        n += 1; sx += x; sy += y; sxx += x * x; sxy += x * y;
                    }

                    const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
                    const auto intercept = (sy - slope * sx) / n;
                    auto worst = 0.0;

                    for (const auto& s : samples)
                        worst = std::max (worst, std::abs (s.*value - (intercept + slope * (s.wall - x0))));

                    return std::pair { slope, worst * 1000.0 };
                };

                const auto [heardSpeed, heardJitterMs] = fit (&Sample::heard);
                const auto renderedJitterMs = fit (&Sample::rendered).second;

                auto lag = 0.0;

                for (const auto& s : samples)
                    lag += s.rendered - s.heard;

                lag /= (double) samples.size();

                auto* device = audio::getDeviceManager (session).getCurrentAudioDevice();
                const auto bufferMs = device != nullptr ? 1000.0 * device->getCurrentBufferSizeSamples() / device->getCurrentSampleRate() : 0.0;
                const auto latencyMs = clock.getLatencySeconds() * 1000.0;

                report.line ("       " + juce::String ((int) samples.size()) + " readings over 2 s; the engine position steps by up to "
                             + juce::String (renderedJitterMs, 1) + " ms (buffer " + juce::String (bufferMs, 1) + " ms)");
                report.check (std::abs (heardSpeed - 1.0) < 0.01 && heardJitterMs < 2.0,
                              "Played notes are timed smoothly: jitter " + juce::String (heardJitterMs, 2)
                                  + " ms, speed " + juce::String (heardSpeed, 4));
                report.check (std::abs (lag * 1000.0 - latencyMs) < bufferMs + 2.0,
                              "Notes go where they were heard: " + juce::String (lag * 1000.0, 1)
                                  + " ms before the rendered position (audio latency " + juce::String (latencyMs, 1) + " ms)");
            }

            void checkRender (core::Project& project, const juce::String& fileName)
            {
                const auto file = options.renderDirectory.getChildFile (fileName);
                file.deleteFile();

                // The file must not be silent even when the speakers are.
                setQuiet (project, false);
                const auto ok = te::Renderer::renderToFile (project.getEdit(), file, false) && file.existsAsFile();
                setQuiet (project, options.quiet);
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

            /** A sound file dragged in: it becomes a clip on an audio track and is heard when the
                song plays (and in the render and export after this). */
            void checkAudioClip (core::Project& project, audio::Transport& transport)
            {
                options.renderDirectory.createDirectory();
                const auto file = options.renderDirectory.getChildFile ("tone.wav");
                report.check (writeTone (file, 2.0), "Wrote a test sound file -> " + file.getFullPathName());

                auto* track = project.addTrack ("Audio");

                if (track == nullptr)
                {
                    report.check (false, "Added an audio track");
                    return;
                }

                core::tracks::setKind (*track, core::tracks::Kind::audio);
                juce::String error;
                auto* clip = project.addAudioClip (*track, file, 0.0, error);
                report.check (clip != nullptr, "The sound file became an audio clip"
                                                   + (error.isEmpty() ? juce::String() : " (" + error + ")"));

                if (clip == nullptr || ! options.useDevice || audio::getDeviceManager (session).getCurrentAudioDevice() == nullptr)
                    return;

                TrackLevel audioLevel (*track);
                transport.setPositionSeconds (0.0);
                transport.play();
                pump (1500, audioLevel, transport, report);
                const auto peak = audioLevel.takePeakDb();
                report.check (peak > -30.0f, "The audio clip is heard while the song plays (peak "
                                                 + juce::String (peak, 1) + " dBFS)");
                transport.stop();
                pump (200, audioLevel, transport, report);
            }

            /** A reverb return added while playing hears what track 1 sends it; deleting it again
                keeps the song playing. */
            void checkSends (core::Project& project, te::AudioTrack& track, audio::Transport& transport, TrackLevel& level)
            {
                transport.setPositionSeconds (0.0);
                transport.play();
                pump (300, level, transport, report);

                juce::String error;
                auto* reverb = audio::sends::addReturnTrack (project, audio::sends::Preset::reverb, error);

                if (reverb == nullptr)
                {
                    report.check (false, "Added a reverb return track: " + error);
                    transport.stop();
                    return;
                }

                {
                    TrackLevel reverbLevel (*reverb);
                    pump (600, reverbLevel, transport, report);
                    const auto before = reverbLevel.takePeakDb();

                    project.beginGesture ("Send to Reverb");
                    const auto sent = audio::sends::setLevelDb (track, *reverb, 0.0f);
                    pump (1500, reverbLevel, transport, report);
                    const auto after = reverbLevel.takePeakDb();

                    report.check (sent && before < -90.0f && after > -60.0f,
                                  "Track 1 sends to a reverb return while playing (return peak "
                                      + juce::String (before, 1) + " dBFS before the send, " + juce::String (after, 1) + " after)");
                }

                audio::sends::removeReturnTrack (project, *reverb);
                level.takePeakDb();
                pump (800, level, transport, report);
                report.check (project.getReturnTracks().isEmpty() && audio::sends::getBus (track) < 0
                                  && transport.isPlaying() && level.takePeakDb() > -60.0f,
                              "Deleted the return track while playing, still audible");

                transport.stop();
                pump (300, level, transport, report);
            }

            /** A volume line drawn on track 1 while it plays: silent where the line is at the
                bottom; the song comes back once the line is cleared. */
            void checkAutomation (core::Project& project, te::AudioTrack& track, audio::Transport& transport, TrackLevel& level)
            {
                transport.setPositionSeconds (0.0);
                transport.play();
                pump (300, level, transport, report);
                level.takePeakDb();
                pump (600, level, transport, report);
                const auto before = level.takePeakDb();

                auto& volume = *track.getVolumePlugin()->volParam;
                project.beginGesture ("Draw a volume line");
                audio::automation::addPoint (volume, 0.0, 0.0f);
                audio::automation::addPoint (volume, songBeats, 0.0f);
                pump (300, level, transport, report);
                level.takePeakDb();
                pump (800, level, transport, report);
                const auto silenced = level.takePeakDb();

                project.beginGesture ("Clear the volume line");
                audio::automation::clear (volume);
                pump (300, level, transport, report);
                level.takePeakDb();
                pump (800, level, transport, report);
                const auto after = level.takePeakDb();

                report.check (before > -60.0f && silenced < -90.0f && after > -60.0f,
                              "A volume line silences track 1 while playing; clearing it brings it back (peak "
                                  + juce::String (before, 1) + " / " + juce::String (silenced, 1) + " / "
                                  + juce::String (after, 1) + " dBFS)");

                transport.stop();
                pump (300, level, transport, report);
            }

            /** Recording from the audio interface's first input onto an armed audio track: a clip
                as long as the recording appears at Stop. (The file is deleted right away.) */
            void checkAudioRecording (core::Project& project, audio::Transport& transport)
            {
                auto& engine = project.getEdit().engine;
                const auto inputs = audio::AudioInputs::getInputs (engine);

                if (inputs.empty())
                {
                    report.line ("       no audio inputs turned on: recording not checked");
                    return;
                }

                auto* track = project.addTrack ("Mic");

                if (track == nullptr)
                    return;

                core::tracks::setKind (*track, core::tracks::Kind::audio);
                audio::AudioInputs audioInputs (project.getEdit());
                audio::AudioInputs::setArmed (*track, true);
                audioInputs.refresh();
                project.getEdit().dispatchPendingUpdatesSynchronously();

                transport.recordsAudio = [&audioInputs] { return audioInputs.anyArmed(); };
                transport.setPositionSeconds (0.0);
                transport.play();
                TrackLevel micLevel (*track);
                pump (1500, micLevel, transport, report);
                const auto wasRecording = transport.isRecording();
                transport.stop();
                transport.recordsAudio = nullptr;
                pump (300, micLevel, transport, report);

                const auto recorded = core::clips::getClips (*track);
                auto* clip = recorded.isEmpty() ? nullptr : dynamic_cast<te::AudioClipBase*> (recorded.getFirst());
                const auto seconds = clip != nullptr ? clip->getPosition().getLength().inSeconds() : 0.0;
                report.check (wasRecording && clip != nullptr && seconds > 1.0,
                              "Recorded from " + inputs.front()->getAliasOrName() + " onto an armed audio track ("
                                  + juce::String (seconds, 2) + " s clip)");

                if (clip != nullptr)
                {
                    const auto file = clip->getCurrentSourceFile();
                    report.line ("       recording went to " + file.getFullPathName());
                    project.getEdit().deleteTrack (track);
                    engine.getAudioFileManager().releaseAllFiles();
                    file.deleteFile();

                    // Don't leave an empty recordings folder behind either.
                    if (const auto folder = file.getParentDirectory();
                        folder.isDirectory() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
                        folder.deleteFile();
                }
                else
                {
                    project.getEdit().deleteTrack (track);
                }
            }

            /** A 220 Hz tone at -6 dBFS. */
            static bool writeTone (const juce::File& file, double seconds)
            {
                constexpr double sampleRate = 44100.0;
                file.deleteFile();

                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                juce::WavAudioFormat wav;
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                               .withSampleRate (sampleRate)
                                                               .withNumChannels (1)
                                                               .withBitsPerSample (16));

                if (writer == nullptr)
                    return false;

                juce::AudioBuffer<float> buffer (1, (int) (seconds * sampleRate));

                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sampleRate));

                return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            }

            /** File > Export WAV: the song plus its tail, with the device running. */
            void checkExport (core::Project& project, const juce::String& fileName)
            {
                const auto file = options.renderDirectory.getChildFile (fileName);
                file.deleteFile();

                setQuiet (project, false);
                const auto error = audio::exportWav (project.getEdit(), file);
                setQuiet (project, options.quiet);
                report.check (error.isEmpty() && file.existsAsFile(), "Export WAV -> " + file.getFullPathName()
                                                                         + (error.isEmpty() ? juce::String() : " (" + error + ")"));

                if (! error.isEmpty())
                    return;

                const auto stats = analyse (file);
                const auto expected = project.getEdit().getLength().inSeconds() + audio::exportTailSeconds;

                report.check (stats.peak > 0.01f, "Exported audio is not silent (peak "
                                                      + juce::String (juce::Decibels::gainToDecibels (stats.peak), 1) + " dBFS)");
                report.check (std::abs (stats.seconds - expected) < 0.1, "Exported length " + juce::String (stats.seconds, 2)
                                                                             + " s = song + tail (" + juce::String (expected, 2) + " s)");
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
        options.quiet = args.contains ("--quiet");
        return options;
    }

    int runSelfTest (core::Session& session, const SelfTestOptions& options)
    {
        return SelfTest (session, options).run();
    }
}
