#include "sandbox/TestPluginSong.h"
#include "audio/Export.h"

using namespace harmonia;
namespace safety = audio::plugin_safety;

namespace
{
    using test::plugins::SynthSong;

    /** The song exported: the peak of its first second (the note), and an error if it failed. */
    float exportPeak (te::Edit& edit)
    {
        juce::TemporaryFile file (".wav");
        const auto error = audio::exportWav (edit, file.getFile());
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->sampleRate);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer.getMagnitude (0, buffer.getNumSamples());
    }

    /** No block of the note is missing: the second half second is as loud as the first. */
    bool playsWithoutGaps (te::Edit& edit)
    {
        juce::TemporaryFile file (".wav");
        REQUIRE (audio::exportWav (edit, file.getFile()).isEmpty());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);

        // The test synth plays a constant level while the note is held: no sample of the
        // first three seconds (the note lasts four) falls to silence.
        const auto length = (int) (3.0 * reader->sampleRate);
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
        reader->read (&buffer, 0, length, 0, true, true);

        const auto start = (int) (0.05 * reader->sampleRate);    // past the fade-in of the note's start

        for (int i = start; i < length; ++i)
            if (std::abs (buffer.getSample (0, i)) < 1.0e-3f)
                return false;

        return true;
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }
}

TEST_SUITE ("audio::plugin_safety")
{
    TEST_CASE ("a song's plugins run in processes of their own; the built-in ones in Harmonia's")
    {
        SynthSong song;
        CHECK (safety::getStatus (song.instrument()) == safety::Status::running);
        CHECK (safety::getRemote (song.instrument()) != nullptr);
        CHECK (! safety::wasHung (song.instrument()));
        CHECK (safety::reload (song.instrument()).isEmpty());   // nothing to do

        REQUIRE (audio::instruments::useBuiltInSynth (song.track).isEmpty());
        CHECK (safety::getStatus (song.instrument()) == safety::Status::notSandboxed);
        CHECK (safety::getRemote (song.instrument()) == nullptr);
    }

    TEST_CASE ("a song with a plugin in its own process exports whole, every block of it")
    {
        SynthSong song;
        CHECK (playsWithoutGaps (song.project->getEdit()));
    }

    TEST_CASE ("a plugin that crashes doesn't stop the export; it is reported, then reloaded as it was")
    {
        SynthSong song;
        int changes = 0;
        safety::Watcher watcher (test::session(), [&] (te::Plugin& plugin)
        {
            if (&plugin == &song.instrument())
                ++changes;
        });

        CHECK (exportPeak (song.project->getEdit()) > 0.1f);

        song.parameter (test::plugins::level).setValueNotifyingHost (0.25f);
        song.parameter (test::plugins::crash).setValueNotifyingHost (1.0f);
        CHECK (exportPeak (song.project->getEdit()) == 0.0f);

        for (int i = 0; i < 100 && changes == 0; ++i)
            pump (20);

        CHECK (changes == 1);
        CHECK (safety::getStatus (song.instrument()) == safety::Status::crashed);
        CHECK (! safety::wasHung (song.instrument()));

        const auto message = safety::describeCrash (song.instrument());
        CHECK (message.contains ("Test Synth"));
        CHECK (message.contains ("crashed"));
        CHECK (message.contains ("instrument"));

        song.parameter (test::plugins::crash).setValueNotifyingHost (0.0f);
        CHECK (safety::reload (song.instrument()).isEmpty());
        CHECK (safety::getStatus (song.instrument()) == safety::Status::running);
        CHECK (changes == 2);

        // As it was: its level is the one it had before the crash.
        CHECK (song.parameter (test::plugins::level).getValue() == doctest::Approx (0.25));
        CHECK (exportPeak (song.project->getEdit()) > 0.05f);
    }

    TEST_CASE ("a plugin whose file is gone is not started in a process")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();

        juce::PluginDescription missing;
        missing.name = "Missing Synth";
        missing.pluginFormatName = "VST3";
        missing.fileOrIdentifier = "C:/does/not/exist/Missing Synth.vst3";
        missing.isInstrument = true;

        const auto error = audio::instruments::useExternal (track, missing);
        CHECK (error.isNotEmpty());
    }
}
