#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <atomic>
#include <iostream>

#include "TestSession.h"

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
}

int main (int argc, char** argv)
{
    // The engine needs JUCE's message manager; it must outlive every engine object.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    harmonia::test::AssertionCatcher assertions;
    juce::Logger::setCurrentLogger (&assertions);

    int result = 0;

    {
        harmonia::core::Session session (harmonia::core::Session::Options { false, nullptr });
        harmonia::test::sharedSession = &session;

        doctest::Context context (argc, argv);
        result = context.run();

        harmonia::test::sharedSession = nullptr;
    }

    juce::Logger::setCurrentLogger (nullptr);

    if (assertions.count > 0)
    {
        std::cerr << assertions.count << " JUCE assertion(s) failed" << std::endl;
        result = result != 0 ? result : 1;
    }

    return result;
}
