#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "TestSession.h"

namespace harmonia::test
{
    namespace
    {
        core::Session* sharedSession = nullptr;
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
    int result = 0;

    {
        harmonia::core::Session session (harmonia::core::Session::Options { false, nullptr });
        harmonia::test::sharedSession = &session;

        doctest::Context context (argc, argv);
        result = context.run();

        harmonia::test::sharedSession = nullptr;
    }

    return result;
}
