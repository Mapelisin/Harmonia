#include "audio/BuiltInPlugins.h"

#include "audio/DrumKit.h"
#include "audio/Limiter.h"
#include "audio/Pump.h"
#include "audio/Sampler.h"
#include "audio/TrackDelay.h"

namespace harmonia::audio
{
    void registerPluginTypes (te::Engine& engine)
    {
        limiter::registerType (engine);
        pump::registerType (engine);
        drum_kit::registerType (engine);
        sampler::registerType (engine);
        track_delay::registerType (engine);
    }
}
