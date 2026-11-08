#pragma once

#include "core/Tracktion.h"

namespace harmonia::core
{
    /** Owns the Tracktion Engine instance shared by the model (core) and audio layers.
        One Session lives for the whole lifetime of the application. */
    class Session
    {
    public:
        struct Options
        {
            /** False for tests and tools: no audio or MIDI devices are opened. */
            bool openDevices = true;

            /** How the engine opens plugin windows etc.; a default one is used if empty. */
            std::unique_ptr<te::UIBehaviour> uiBehaviour;
        };

        explicit Session (Options);
        ~Session();

        te::Engine& getEngine() noexcept           { return *engine; }
        bool devicesEnabled() const noexcept       { return openDevices; }

    private:
        bool openDevices;
        std::unique_ptr<te::Engine> engine;

        JUCE_DECLARE_NON_COPYABLE (Session)
    };
}
