#pragma once

#include "core/Session.h"

namespace harmonia::test
{
    /** The headless Session shared by all tests (no audio devices are opened).
        Created in TestMain after JUCE is initialised. */
    core::Session& session();
}
