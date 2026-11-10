#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <atomic>
#include <iostream>

#include "TestSession.h"
#include "audio/DrumKit.h"
#include "audio/Sampler.h"
#include "audio/Limiter.h"
#include "audio/Pump.h"
#include "sandbox/Host.h"
#include "sandbox/Platform.h"
#include "sandbox/TestPlugins.h"
#include "ui/Theme.h"

namespace harmonia::test
{
    namespace
    {
        core::Session* sharedSession = nullptr;

        /** In Debug builds JUCE_LOG_ASSERTIONS sends every jassert failure here
            (from any thread). Any such failure fails the test run. */
        struct AssertionCatcher : juce::Logger
        {
            void logMessage (const juce::String& message) override
            {
                if (message.startsWith ("JUCE Assertion failure"))
                {
                    ++count;
                    std::cerr << message << std::endl;
                }
            }

            std::atomic<int> count { 0 };
        };
    }

    core::Session& session()
    {
        jassert (sharedSession != nullptr);
        return *sharedSession;
    }

    namespace
    {
        /** The test program is also the process each plugin of the tests runs in: there it can
            load the test plugins too. */
        int runPluginHost (sandbox::Host::Options options)
        {
            juce::ScopedJuceInitialiser_GUI juceInitialiser;
            sandbox::platform::prepareHostProcess();

            options.addFormats = [] (juce::AudioPluginFormatManager& formats)
            {
                juce::addDefaultFormatsToManager (formats);
                formats.addFormat (plugins::createFormat());
            };

            options.onFinished = [] { juce::MessageManager::getInstance()->stopDispatchLoop(); };

            {
                sandbox::Host host (std::move (options));
                juce::MessageManager::getInstance()->runDispatchLoop();
            }

            return 0;
        }
    }
}

int main (int argc, char** argv)
{
    {
        juce::StringArray arguments;

        for (int i = 0; i < argc; ++i)
            arguments.add (juce::String::fromUTF8 (argv[i]));

        if (auto hostOptions = harmonia::sandbox::Host::fromCommandLine (arguments))
            return harmonia::test::runPluginHost (std::move (*hostOptions));
    }

    // Whatever is printed is out at once: when a test hangs, the log says which ran before it.
    std::cout.setf (std::ios::unitbuf);

    // The engine needs JUCE's message manager; it must outlive every engine object.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    harmonia::test::AssertionCatcher assertions;
    juce::Logger::setCurrentLogger (&assertions);

    // The program's own look, so components measure and paint (and snapshots look) as in the app.
    harmonia::ui::theme::LookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    int result = 0;

    // The sound files the tests have the program make (tracks rendered or frozen, clips joined, loops
    // cut up) go to a folder of this run's own, gone when it is over: never among the user's own
    // recordings in Documents, where a session without one would put them.
    const auto recordings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getNonexistentChildFile ("harmonia-test-recordings-" + juce::Uuid().toString().substring (0, 12), {}, false);

    {
        harmonia::core::Session::Options options;
        options.openDevices = false;
        options.settingsName = "HarmoniaTests";
        options.recordingsFolder = recordings;
        options.pluginHost = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        options.pluginHangLimitMs = 1000;

        harmonia::core::Session session (std::move (options));

        // Plugins in processes of their own, whatever an interrupted run left in the settings; the
        // test plugins can load here too (for the tests that turn that off).
        session.setPluginSandboxOn (true);
        session.getEngine().getPluginManager().pluginFormatManager.addFormat (harmonia::test::plugins::createFormat());
        harmonia::audio::limiter::registerType (session.getEngine());
        harmonia::audio::pump::registerType (session.getEngine());
        harmonia::audio::drum_kit::registerType (session.getEngine());
        harmonia::audio::sampler::registerType (session.getEngine());
        harmonia::test::sharedSession = &session;

        doctest::Context context (argc, argv);
        result = context.run();

        harmonia::test::sharedSession = nullptr;
    }

    recordings.deleteRecursively();

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    juce::Logger::setCurrentLogger (nullptr);

    if (assertions.count > 0)
    {
        std::cerr << assertions.count << " JUCE assertion(s) failed" << std::endl;
        result = result != 0 ? result : 1;
    }

    return result;
}
