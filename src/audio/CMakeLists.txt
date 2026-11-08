add_library(harmonia_audio STATIC
    DeviceStatus.h
    DeviceStatus.cpp)

harmonia_configure_target(harmonia_audio)
target_link_libraries(harmonia_audio PUBLIC harmonia_core)
