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
#include "core/Project.h"

using namespace harmonia;
namespace instruments = audio::instruments;

namespace
{
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
        CHECK (seconds == doctest::Approx (4.0 + audio::exportTailSeconds).epsilon (0.02));

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        CHECK (buffer.getMagnitude (0, (int) reader->sampleRate) > 0.01f);
    }

    TEST_CASE ("an empty song is not exported")
    {
        auto project = core::Project::createEmpty (test::session());
        juce::TemporaryFile file (".wav");

        CHECK (audio::exportWav (project->getEdit(), file.getFile()).contains ("Nothing to export"));
        CHECK_FALSE (file.getFile().existsAsFile());
    }
}
