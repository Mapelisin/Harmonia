add_library(harmonia_audio STATIC
    DeviceStatus.h
    DeviceStatus.cpp
    DeviceSetup.h
    DeviceSetup.cpp
    Instruments.h
    Instruments.cpp
    NotePreview.h
    NotePreview.cpp
    PluginScanner.h
    PluginScanner.cpp
    Transport.h
    Transport.cpp)

harmonia_configure_target(harmonia_audio)
target_link_libraries(harmonia_audio PUBLIC harmonia_core)
