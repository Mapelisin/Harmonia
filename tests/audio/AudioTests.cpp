#include <doctest.h>

#include "TestSession.h"
#include "audio/DeviceSetup.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/MidiInputs.h"
#include "audio/NotePreview.h"
#include "audio/PlaybackClock.h"
#include "audio/PluginScanner.h"
#include "audio/Transport.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Controllers.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "TestAudioFiles.h"

using namespace harmonia;
namespace instruments = audio::instruments;

namespace
{
    /** The song exported, its first channel. */
    juce::AudioBuffer<float> exportFirstChannel (te::Edit& edit, double& sampleRate)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (edit, file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);
        sampleRate = reader->sampleRate;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        buffer.setSize (1, buffer.getNumSamples(), true);
        return buffer;
    }

    /** The pitch of the sound between the two times, from where it best repeats: around A4
        (300..700 Hz, so neither an octave down nor up can be mistaken for it). */
    double pitchBetween (const juce::AudioBuffer<float>& buffer, double sampleRate, double from, double to)
    {
        const auto* x = buffer.getReadPointer (0);
        const auto start = (int) (from * sampleRate), length = (int) ((to - from) * sampleRate);
        auto bestLag = 0;
        auto best = -1.0;

        for (auto lag = (int) (sampleRate / 700.0); lag <= (int) (sampleRate / 300.0); ++lag)
        {
            double sum = 0.0, energy = 0.0;

            for (int i = start; i < start + length; ++i)
            {
                sum += (double) x[i] * x[i + lag];
                energy += (double) x[i + lag] * x[i + lag];
            }

            if (const auto score = energy > 0.0 ? sum / std::sqrt (energy) : 0.0; score > best)
            {
                best = score;
                bestLag = lag;
            }
        }

        return bestLag > 0 ? sampleRate / bestLag : 0.0;
    }

    int countSynths (te::AudioTrack& track)
    {
        int n = 0;

        for (auto* plugin : track.pluginList.getPlugins())
            n += plugin->isSynth() ? 1 : 0;

        return n;
    }
}

TEST_SUITE ("audio::instruments")
{
    TEST_CASE ("the built-in synth becomes the track's only instrument")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();

        CHECK (instruments::get (track) == nullptr);

        CHECK (instruments::useBuiltInSynth (track).isEmpty());
        CHECK (dynamic_cast<te::FourOscPlugin*> (instruments::get (track)) != nullptr);

        CHECK (instruments::useBuiltInSynth (track).isEmpty());
        CHECK (countSynths (track) == 1);
    }

    TEST_CASE ("a plugin that fails to load leaves the current instrument in place")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (instruments::useBuiltInSynth (track).isEmpty());
        auto* before = instruments::get (track);

        juce::PluginDescription missing;
        missing.name = "Missing Synth";
        missing.pluginFormatName = "VST3";
        missing.fileOrIdentifier = "C:/does/not/exist/Missing Synth.vst3";
        missing.isInstrument = true;

        CHECK (instruments::useExternal (track, missing).isNotEmpty());
        CHECK (instruments::get (track) == before);
        CHECK (countSynths (track) == 1);
    }

    TEST_CASE ("an effect is refused as an instrument")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();

        juce::PluginDescription effect;
        effect.name = "Some Reverb";
        effect.isInstrument = false;

        CHECK (instruments::useExternal (track, effect).contains ("not an instrument"));
        CHECK (instruments::get (track) == nullptr);
    }
}

TEST_SUITE ("audio without a device")
{
    TEST_CASE ("a headless session reports no start-up device problem")
    {
        CHECK (audio::checkStartupDevice (test::session()).isEmpty());
    }

    TEST_CASE ("play waits for a device instead of failing")
    {
        auto project = core::Project::createEmpty (test::session());
        audio::Transport transport (project->getEdit());

        transport.play();
        CHECK (transport.isPlayRequested());
        CHECK_FALSE (transport.isPlaying());

        transport.stop();
        CHECK_FALSE (transport.isPlayRequested());
        CHECK_FALSE (transport.isPlaying());
    }

    TEST_CASE ("note preview is safe without a device and after its track is gone")
    {
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->addTrack ("Lead");
        REQUIRE (track != nullptr);
        REQUIRE (instruments::useBuiltInSynth (*track).isEmpty());

        audio::NotePreview preview;
        preview.setTrack (track);
        CHECK (preview.getTrack() == track);

        preview.noteOn (60, 100);
        preview.noteOn (64, 100);
        preview.noteOff (60);

        project->getEdit().deleteTrack (track);
        CHECK (preview.getTrack() == nullptr);

        preview.noteOn (67, 100);
        preview.allNotesOff();
    }

    TEST_CASE ("files that can't be plugins are not even opened by the scanner")
    {
        audio::PluginScanner scanner (test::session());
        juce::TemporaryFile notAPlugin (".txt");
        REQUIRE (notAPlugin.getFile().replaceWithText ("not a plugin"));

        CHECK (scanner.scanFile (notAPlugin.getFile().getFullPathName()).isEmpty());
    }

    TEST_CASE ("a scan also looks in the plugin folders of one's own, kept in the settings")
    {
        auto& session = test::session();
        const auto before = audio::PluginScanner::getOwnFolders (session);
        const juce::TemporaryFile folder;
        REQUIRE (folder.getFile().createDirectory());

        audio::PluginScanner::addFolder (session, folder.getFile());
        audio::PluginScanner::addFolder (session, folder.getFile());
        audio::PluginScanner::addFolder (session, folder.getFile().getChildFile ("missing"));

        const auto own = audio::PluginScanner::getOwnFolders (session);
        CHECK (own.size() == before.size() + 1);
        CHECK (own.contains (folder.getFile().getFullPathName()));

        audio::PluginScanner scanner (session);
        auto* vst3 = scanner.getFormatsToScan().getFirst();
        REQUIRE (vst3 != nullptr);

        const auto searches = [&]
        {
            const auto path = audio::PluginScanner::getSearchPath (session, *vst3);

            for (int i = 0; i < path.getNumPaths(); ++i)
                if (path[i] == folder.getFile())
                    return true;

            return false;
        };

        CHECK (audio::PluginScanner::getSearchPath (session, *vst3).getNumPaths()
                 == vst3->getDefaultLocationsToSearch().getNumPaths() + 1);
        CHECK (searches());

        audio::PluginScanner::removeFolder (session, folder.getFile().getFullPathName());
        CHECK (audio::PluginScanner::getOwnFolders (session) == before);
        CHECK (! searches());

        folder.getFile().deleteRecursively();
    }
}

TEST_SUITE ("audio::PlaybackClock")
{
    TEST_CASE ("a note belongs where the listener heard the song: the latency earlier, wrapped into the loop")
    {
        using audio::PlaybackClock;
        const juce::Range<double> loop { 2.0, 10.0 };

        // 46 ms of output plus plugin latency.
        CHECK (PlaybackClock::toHeardSeconds (5.0, 0.046, {}) == doctest::Approx (4.954));
        CHECK (PlaybackClock::toHeardSeconds (5.0, 0.046, loop) == doctest::Approx (4.954));

        // Rendering already wrapped to the loop start, the speakers still play its end.
        CHECK (PlaybackClock::toHeardSeconds (2.01, 0.046, loop) == doctest::Approx (9.964));

        // Never before the song start.
        CHECK (PlaybackClock::toHeardSeconds (0.01, 0.046, {}) == doctest::Approx (0.0));
    }

    TEST_CASE ("a stopped transport has no heard position")
    {
        auto project = core::Project::createEmpty (test::session());
        audio::PlaybackClock clock (project->getEdit());

        CHECK_FALSE (clock.isPlaying());
        CHECK_FALSE (clock.getHeardBeat (juce::Time::getMillisecondCounterHiRes()).has_value());
        CHECK (clock.getLatencySeconds() >= 0.0);
    }
}

TEST_SUITE ("audio::MidiInputs")
{
    TEST_CASE ("without MIDI keyboards routing is a no-op")
    {
        auto project = core::Project::createEmpty (test::session());
        audio::MidiInputs inputs (project->getEdit());

        inputs.setTrack (project->getTracks().getFirst());
        inputs.setTrack (nullptr);
        CHECK (inputs.getDeviceNames().isEmpty());
    }

    TEST_CASE ("notes, pedals, wheels, knobs and key pressure reach the program in the order sent; clocks and sysex don't")
    {
        using audio::MidiInputs;
        CHECK (MidiInputs::isForwarded (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)));
        CHECK (MidiInputs::isForwarded (juce::MidiMessage::noteOff (1, 60)));
        CHECK (MidiInputs::isForwarded (juce::MidiMessage::controllerEvent (1, 64, 127)));      // the pedal
        CHECK (MidiInputs::isForwarded (juce::MidiMessage::controllerEvent (1, 1, 50)));        // the mod wheel
        CHECK (MidiInputs::isForwarded (juce::MidiMessage::controllerEvent (3, 21, 50)));       // a knob: MIDI Learn needs it
        CHECK (MidiInputs::isForwarded (juce::MidiMessage::pitchWheel (1, 9000)));
        CHECK (MidiInputs::isForwarded (juce::MidiMessage::channelPressureChange (1, 90)));
        CHECK_FALSE (MidiInputs::isForwarded (juce::MidiMessage::midiClock()));
        CHECK_FALSE (MidiInputs::isForwarded (juce::MidiMessage (0xfe)));                        // active sensing
        CHECK_FALSE (MidiInputs::isForwarded (juce::MidiMessage::programChange (1, 5)));

        auto project = core::Project::createEmpty (test::session());
        MidiInputs inputs (project->getEdit());
        std::vector<juce::MidiMessage> received;
        inputs.onMessage = [&received] (const juce::MidiMessage& m, double) { received.push_back (m); };

        inputs.post (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
        inputs.post (juce::MidiMessage::midiClock());
        inputs.post (juce::MidiMessage::controllerEvent (2, 21, 64));
        inputs.post (juce::MidiMessage::pitchWheel (1, 9000));
        inputs.post (juce::MidiMessage::channelPressureChange (1, 90));
        CHECK (received.empty());       // handed over on the message thread, not on the keyboard's

        for (int i = 0; i < 20 && received.size() < 4; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        REQUIRE (received.size() == 4);
        CHECK (received[0].isNoteOn());
        CHECK (received[1].isControllerOfType (21));
        CHECK (received[1].getChannel() == 2);
        CHECK (received[2].isPitchWheel());
        CHECK (received[3].isChannelPressure());
    }
}

TEST_SUITE ("audio::exportWav")
{
    TEST_CASE ("the song is exported to a WAV file as long as the song plus the tail, and it isn't silent")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (instruments::useBuiltInSynth (track).isEmpty());

        // Two bars at 120 BPM = 4 seconds, a chord in the first bar.
        auto* clip = project->addMidiClip (track, 0.0, 8.0, "Chord");
        REQUIRE (clip != nullptr);

        for (int pitch : { 60, 64, 67 })
            clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (2.0), 110, 0, nullptr);

        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (project->getEdit(), file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());
        REQUIRE (file.getFile().existsAsFile());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);
        CHECK (reader->getFormatName().containsIgnoreCase ("wav"));

        const auto seconds = (double) reader->lengthInSamples / reader->sampleRate;
        // Until the chord has rung out after the clip's end: no fixed tail.
        CHECK (seconds > 4.0 + 0.25);
        CHECK (seconds < 4.0 + 3.0);

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        CHECK (buffer.getMagnitude (0, (int) reader->sampleRate) > 0.01f);
    }

    TEST_CASE ("a song with FLAC and Ogg sounds is exported with them, stretched to the tempo too")
    {
        // The engine plays such files from a WAV copy it makes in the background: the export has to
        // have them all the same, not wait for the copy forever or leave them out.
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        core::tracks::setKind (track, core::tracks::Kind::audio);
        REQUIRE (project->save (temp.getFile ("Song.harmonia")).isEmpty());     // its sound files go next to it

        const auto wav = test::writeTone (temp.getFile ("Tone.wav"), 2.0, 0.5f);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        for (const auto* extension : { ".flac", ".ogg" })
        {
            CAPTURE (extension);
            const auto file = temp.getFile (juce::String ("Tone") + extension);
            {
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
                REQUIRE (reader != nullptr);
                auto* format = formats.findFormatForFileExtension (extension);
                REQUIRE (format != nullptr);
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                auto writer = format->createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                                   .withSampleRate (reader->sampleRate)
                                                                   .withNumChannels (1)
                                                                   .withBitsPerSample (16)
                                                                   .withQualityOptionIndex (format->getQualityOptions().isEmpty() ? 0 : 5));
                REQUIRE (writer != nullptr);
                REQUIRE (writer->writeFromAudioReader (*reader, 0, reader->lengthInSamples));
            }

            for (auto* clip : core::clips::getClips (track))
                clip->removeFromParent();

            juce::String error;
            auto* clip = project->addAudioClip (track, file, 0.0, error);
            REQUIRE_MESSAGE (clip != nullptr, error.toStdString());

            const auto exportedLevel = [&]
            {
                juce::TemporaryFile out (".wav");
                REQUIRE (audio::exportWav (project->getEdit(), out.getFile()).isEmpty());
                std::unique_ptr<juce::AudioFormatReader> result (formats.createReaderFor (out.getFile()));
                REQUIRE (result != nullptr);
                juce::AudioBuffer<float> buffer ((int) result->numChannels, (int) result->lengthInSamples);
                result->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                return buffer.getRMSLevel (0, (int) (0.2 * result->sampleRate), (int) (1.2 * result->sampleRate));
            };

            CHECK (exportedLevel() > 0.1f);

            // Stretched while it plays, it's read from a WAV of it next to the song.
            core::audio_tempo::setFollowsTempo (*clip, 100.0);
            CHECK (clip->getAudioFile().getFile().hasFileExtension ("wav"));
            CHECK (clip->getAudioFile().getFile().isAChildOf (temp.getFile ("Song Audio")));
            CHECK (exportedLevel() > 0.1f);
        }
    }

    TEST_CASE ("pitch bend drawn in a clip bends the built-in synth's notes; the pedal holds them")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (instruments::useBuiltInSynth (track).isEmpty());
        auto* clip = project->addMidiClip (track, 0.0, 8.0, "Bend");
        REQUIRE (clip != nullptr);

        // A4 for 4 seconds, bent all the way up (2 semitones) from beat 4 (2 seconds).
        clip->getSequence().addNote (69, te::BeatPosition(), te::BeatDuration::fromBeats (8.0), 100, 0, nullptr);
        core::controllers::add (*clip, core::controllers::Controller::pitchBend, 4.0, 1.0f);

        auto sampleRate = 0.0;
        const auto bent = exportFirstChannel (project->getEdit(), sampleRate);
        const auto before = pitchBetween (bent, sampleRate, 0.5, 1.5);
        const auto after = pitchBetween (bent, sampleRate, 2.5, 3.5);
        CHECK (before == doctest::Approx (440.0).epsilon (0.02));
        CHECK (after / before == doctest::Approx (std::pow (2.0, 2.0 / 12.0)).epsilon (0.01));

        // A short note: silent after its release, unless the pedal is down.
        clip->getSequence().clear (nullptr);
        clip->getSequence().addNote (69, te::BeatPosition(), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);
        const auto level = [&]
        {
            const auto sound = exportFirstChannel (project->getEdit(), sampleRate);
            return sound.getRMSLevel (0, (int) (1.5 * sampleRate), (int) (0.5 * sampleRate));
        };

        const auto released = level();
        core::controllers::add (*clip, core::controllers::Controller::sustain, 0.0, 1.0f);
        core::controllers::add (*clip, core::controllers::Controller::sustain, 6.0, 0.0f);
        const auto held = level();
        CHECK (released < 0.001f);
        CHECK (held > 0.01f);
    }

    TEST_CASE ("an empty song is not exported")
    {
        auto project = core::Project::createEmpty (test::session());
        juce::TemporaryFile file (".wav");

        CHECK (audio::exportWav (project->getEdit(), file.getFile()).contains ("Nothing to export"));
        CHECK_FALSE (file.getFile().existsAsFile());
    }
}
