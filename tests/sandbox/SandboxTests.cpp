#include <doctest.h>

#include "TestSession.h"
#include "sandbox/RemotePlugin.h"
#include "sandbox/TestPlugins.h"

using namespace harmonia;

namespace
{
    constexpr double testRate = 44100.0;
    constexpr int blockSize = 256;

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** Lets messages through until `condition` holds (or the time is up): what it says then. */
    template <typename Condition>
    bool waitFor (Condition&& condition, int timeoutMs = 5000)
    {
        for (const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
             ! condition() && juce::Time::getMillisecondCounter() < until;)
            pump (10);

        return condition();
    }

    std::unique_ptr<sandbox::RemotePlugin> load (const juce::PluginDescription& description)
    {
        auto* sandbox = test::session().getPluginSandbox();
        REQUIRE (sandbox != nullptr);

        juce::String error;
        auto plugin = sandbox->createInstance (description, testRate, blockSize, error);
        INFO (error);
        REQUIRE (plugin != nullptr);
        CHECK (error.isEmpty());

        plugin->enableAllBuses();
        plugin->prepareToPlay (testRate, blockSize);
        return plugin;
    }

    juce::AudioProcessorParameter& parameter (juce::AudioPluginInstance& plugin, int index)
    {
        auto* found = plugin.getParameters()[index];
        REQUIRE (found != nullptr);
        return *found;
    }

    /** One block with a note held from `noteAt` (none if negative): the sound, and the MIDI back. */
    juce::AudioBuffer<float> play (juce::AudioPluginInstance& plugin, int noteAt = 0, juce::MidiBuffer* midiBack = nullptr)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        juce::MidiBuffer midi;

        if (noteAt >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), noteAt);

        plugin.processBlock (buffer, midi);

        if (midiBack != nullptr)
            midiBack->swapWith (midi);

        return buffer;
    }

    /** The test gain on a block of `level` everywhere. */
    juce::AudioBuffer<float> playThrough (juce::AudioPluginInstance& plugin, float level, int numChannels = 2)
    {
        juce::AudioBuffer<float> buffer (numChannels, blockSize);

        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), level, blockSize);

        juce::MidiBuffer midi;
        plugin.processBlock (buffer, midi);
        return buffer;
    }

    bool isEverywhere (const juce::AudioBuffer<float>& buffer, float value, int from = 0)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int i = from; i < buffer.getNumSamples(); ++i)
                if (std::abs (buffer.getSample (channel, i) - value) > 1.0e-6f)
                    return false;

        return true;
    }

    struct CrashCounter final : sandbox::Sandbox::Listener
    {
        CrashCounter()     { test::session().getPluginSandbox()->addListener (this); }
        ~CrashCounter() override    { test::session().getPluginSandbox()->removeListener (this); }

        void pluginCrashed (sandbox::RemotePlugin&) override     { ++crashes; }
        void pluginReloaded (sandbox::RemotePlugin&) override    { ++reloads; }

        int crashes = 0, reloads = 0;
    };
}

TEST_SUITE ("sandbox")
{
    TEST_CASE ("a plugin in a process of its own plays here as if it were loaded here")
    {
        auto plugin = load (test::plugins::synth());
        plugin->setNonRealtime (true);

        CHECK (plugin->getName() == "Test Synth");
        CHECK (plugin->acceptsMidi());
        CHECK (plugin->producesMidi());
        CHECK (plugin->hasEditor());
        CHECK (plugin->getTotalNumInputChannels() == 0);
        CHECK (plugin->getTotalNumOutputChannels() == 2);
        CHECK (plugin->getLatencySamples() == 64);
        CHECK (plugin->getParameters().size() == 4);
        CHECK (parameter (*plugin, test::plugins::level).getName (100) == "Level");
        CHECK (parameter (*plugin, test::plugins::level).getValue() == doctest::Approx (0.5));

        // Silent until the note, then Level; its MIDI comes back with it.
        juce::MidiBuffer midiBack;
        const auto sound = play (*plugin, 100, &midiBack);
        CHECK (sound.getMagnitude (0, 100) == 0.0f);
        CHECK (isEverywhere (sound, 0.5f, 100));
        REQUIRE (midiBack.getNumEvents() == 1);
        CHECK ((*midiBack.begin()).samplePosition == 100);
        CHECK ((*midiBack.begin()).getMessage().isNoteOn());

        // A value the plugin changed itself reaches this side.
        CHECK (waitFor ([&] { return std::abs (parameter (*plugin, test::plugins::lastNote).getValue() - 60.0f / 127.0f) < 1.0e-4f; }));

        CHECK (plugin->getNumMissedBlocks() == 0);
        CHECK (! plugin->hasCrashed());
    }

    TEST_CASE ("a plugin's knobs, text, programs and state go between the processes")
    {
        auto plugin = load (test::plugins::synth());
        plugin->setNonRealtime (true);
        auto& level = parameter (*plugin, test::plugins::level);

        CHECK (level.getText (0.25f, 100) == "25%");
        CHECK (level.getValueForText ("40%") == doctest::Approx (0.4));

        level.setValueNotifyingHost (0.8f);
        CHECK (isEverywhere (play (*plugin), 0.8f));

        REQUIRE (plugin->getNumPrograms() == 3);
        CHECK (plugin->getProgramName (1) == "Two");
        plugin->setCurrentProgram (2);
        CHECK (plugin->getCurrentProgram() == 2);
        plugin->changeProgramName (0, "Uno");
        CHECK (plugin->getProgramName (0) == "Uno");

        // A knob turned while nothing plays is in the state all the same.
        level.setValueNotifyingHost (0.3f);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        REQUIRE (state.getSize() > 0);

        auto other = load (test::plugins::synth());
        other->setNonRealtime (true);
        other->setStateInformation (state.getData(), (int) state.getSize());

        CHECK (other->getCurrentProgram() == 2);
        CHECK (waitFor ([&] { return std::abs (parameter (*other, test::plugins::level).getValue() - 0.3f) < 1.0e-4f; }));
        CHECK (isEverywhere (play (*other), 0.3f));
    }

    TEST_CASE ("a plugin that crashes goes silent, Harmonia plays on, and it comes back as it was")
    {
        CrashCounter counter;
        auto plugin = load (test::plugins::synth());
        plugin->setNonRealtime (true);

        parameter (*plugin, test::plugins::level).setValueNotifyingHost (0.7f);
        juce::MemoryBlock before;
        plugin->getStateInformation (before);

        juce::Rectangle<int> closedAt { 1, 1, 1, 1 };
        int closes = 0;
        plugin->onEditorClosed = [&] (juce::Rectangle<int> bounds) { ++closes; closedAt = bounds; };
        CHECK (plugin->showEditor ({ 120, 120 }, "Test Synth", false));

        parameter (*plugin, test::plugins::crash).setValueNotifyingHost (1.0f);
        CHECK (isEverywhere (play (*plugin), 0.0f));

        CHECK (waitFor ([&] { return counter.crashes == 1; }));
        CHECK (plugin->hasCrashed());
        CHECK (! plugin->wasHung());

        // Its window went with its process.
        CHECK (closes == 1);
        CHECK (closedAt.isEmpty());
        CHECK (! plugin->isEditorShowing());

        // Silent from now on, at once, and the song still gets its settings.
        const auto startedAt = juce::Time::getMillisecondCounterHiRes();
        CHECK (isEverywhere (play (*plugin), 0.0f));
        CHECK (juce::Time::getMillisecondCounterHiRes() - startedAt < 50.0);

        juce::MemoryBlock after;
        plugin->getStateInformation (after);
        CHECK (after == before);

        parameter (*plugin, test::plugins::crash).setValueNotifyingHost (0.0f);
        CHECK (plugin->reload().isEmpty());
        CHECK (! plugin->hasCrashed());
        CHECK (counter.reloads == 1);
        CHECK (isEverywhere (play (*plugin), 0.7f));
    }

    TEST_CASE ("a plugin that hangs is stopped: its blocks are silent and Harmonia doesn't wait for it")
    {
        CrashCounter counter;
        auto plugin = load (test::plugins::synth());
        plugin->setNonRealtime (true);
        CHECK (isEverywhere (play (*plugin), 0.5f));
        plugin->setNonRealtime (false);

        parameter (*plugin, test::plugins::hang).setValueNotifyingHost (1.0f);

        // Live, each block waits about its own length at most; the watchdog stops the process.
        double longestMs = 0;

        for (int i = 0; i < 1000 && ! plugin->hasCrashed(); ++i)
        {
            const auto startedAt = juce::Time::getMillisecondCounterHiRes();
            const auto sound = play (*plugin);
            longestMs = juce::jmax (longestMs, juce::Time::getMillisecondCounterHiRes() - startedAt);
            CHECK (isEverywhere (sound, 0.0f));
            pump (5);
        }

        CHECK (plugin->hasCrashed());
        CHECK (plugin->wasHung());
        CHECK (plugin->getNumMissedBlocks() > 0);
        CHECK (longestMs < 100.0);
        CHECK (waitFor ([&] { return counter.crashes == 1; }));

        parameter (*plugin, test::plugins::hang).setValueNotifyingHost (0.0f);
        CHECK (plugin->reload().isEmpty());
        CHECK (! plugin->wasHung());
        plugin->setNonRealtime (true);
        CHECK (isEverywhere (play (*plugin), 0.5f));
    }

    TEST_CASE ("an effect whose process crashed lets its sound through")
    {
        auto plugin = load (test::plugins::gain());
        plugin->setNonRealtime (true);
        CHECK (plugin->getTotalNumInputChannels() == 2);

        CHECK (isEverywhere (playThrough (*plugin, 0.25f), 0.25f));
        parameter (*plugin, test::plugins::level).setValueNotifyingHost (1.0f);
        CHECK (isEverywhere (playThrough (*plugin, 0.25f), 0.5f));

        parameter (*plugin, test::plugins::crash).setValueNotifyingHost (1.0f);
        CHECK (isEverywhere (playThrough (*plugin, 0.25f), 0.25f));
        CHECK (plugin->hasCrashed());
        CHECK (isEverywhere (playThrough (*plugin, 0.25f), 0.25f));
    }

    TEST_CASE ("a plugin's own bus layouts are asked of its process")
    {
        auto plugin = load (test::plugins::gain());
        plugin->setNonRealtime (true);

        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());

        juce::AudioProcessor::BusesLayout surround;
        surround.inputBuses.add (juce::AudioChannelSet::create5point1());
        surround.outputBuses.add (juce::AudioChannelSet::create5point1());

        CHECK (! plugin->checkBusesLayoutSupported (surround));
        REQUIRE (plugin->setBusesLayout (mono));
        plugin->prepareToPlay (testRate, blockSize);

        parameter (*plugin, test::plugins::level).setValueNotifyingHost (1.0f);
        CHECK (isEverywhere (playThrough (*plugin, 0.2f, 1), 0.4f));
    }

    TEST_CASE ("a plugin's window is its process's: shown, then closed where it was")
    {
        auto plugin = load (test::plugins::synth());

        CHECK (plugin->showEditor ({ 140, 160 }, "Test Synth", false));
        CHECK (plugin->isEditorShowing());

        // Shown again it stays one window.
        CHECK (plugin->showEditor ({ 400, 400 }, "Test Synth", true));

        const auto bounds = plugin->hideEditor();
        CHECK (! plugin->isEditorShowing());
        CHECK (bounds.getPosition().getDistanceFrom ({ 140, 160 }) <= 2);
        CHECK (! bounds.isEmpty());
        CHECK (plugin->hideEditor().isEmpty());

        // And it opens again.
        CHECK (plugin->showEditor (bounds.getPosition(), "Test Synth", false));
        CHECK (! plugin->hideEditor().isEmpty());
    }

    TEST_CASE ("a plugin that can't be loaded says why, and no process is left")
    {
        auto description = test::plugins::synth();
        description.fileOrIdentifier = juce::String (test::plugins::formatName) + ":Nothing";

        juce::String error;
        auto plugin = test::session().getPluginSandbox()->createInstance (description, testRate, blockSize, error);
        CHECK (plugin == nullptr);
        CHECK (error.contains ("no test plugin"));
    }
}
