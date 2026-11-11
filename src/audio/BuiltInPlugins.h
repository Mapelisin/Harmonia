#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Lets the engine create Harmonia's own plugins (the limiter, the pump, the drum machine,
        the sampler, a track's delay) and load songs that have them. Call once, right after the
        engine is made. */
    void registerPluginTypes (te::Engine&);
}
