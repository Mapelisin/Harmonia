#include "core/Session.h"

namespace harmonia::core
{
    namespace
    {
        class HarmoniaEngineBehaviour final : public te::EngineBehaviour
        {
        public:
            explicit HarmoniaEngineBehaviour (bool shouldOpenDevices) : openDevices (shouldOpenDevices) {}

            bool autoInitialiseDeviceManager() override { return openDevices; }

        private:
            bool openDevices;
        };
    }

    Session::Session (Options options)
        : openDevices (options.openDevices)
    {
        auto ui = options.uiBehaviour != nullptr ? std::move (options.uiBehaviour)
                                                 : std::make_unique<te::UIBehaviour>();

        engine = std::make_unique<te::Engine> ("Harmonia", std::move (ui),
                                               std::make_unique<HarmoniaEngineBehaviour> (openDevices));
    }

    Session::~Session() = default;
}
