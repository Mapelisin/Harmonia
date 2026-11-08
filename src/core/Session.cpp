#include "core/Session.h"

namespace harmonia::core
{
    namespace
    {
        class HarmoniaEngineBehaviour final : public te::EngineBehaviour
        {
        public:
            HarmoniaEngineBehaviour (bool shouldOpenDevices, bool shouldScanOutOfProcess)
                : openDevices (shouldOpenDevices), scanOutOfProcess (shouldScanOutOfProcess) {}

            bool autoInitialiseDeviceManager() override  { return openDevices; }
            bool canScanPluginsOutOfProcess() override    { return scanOutOfProcess; }

        private:
            bool openDevices, scanOutOfProcess;
        };
    }

    Session::Session (Options options)
        : openDevices (options.openDevices)
    {
        auto ui = options.uiBehaviour != nullptr ? std::move (options.uiBehaviour)
                                                 : std::make_unique<te::UIBehaviour>();

        engine = std::make_unique<te::Engine> (std::make_unique<te::PropertyStorage> (options.settingsName),
                                               std::move (ui),
                                               std::make_unique<HarmoniaEngineBehaviour> (openDevices,
                                                                                          options.scanPluginsOutOfProcess));
    }

    Session::~Session() = default;
}
