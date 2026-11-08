add_library(harmonia_audio STATIC
    DeviceStatus.h
    DeviceStatus.cpp
    DeviceSetup.h
    DeviceSetup.cpp
    Effects.h
    Effects.cpp
    Export.h
    Export.cpp
    Instruments.h
    Instruments.cpp
    MidiInputs.h
    MidiInputs.cpp
    NotePreview.h
    NotePreview.cpp
    PlaybackClock.h
    PlaybackClock.cpp
    PluginControls.h
    PluginControls.cpp
    PluginScanner.h
    PluginScanner.cpp
    Transport.h
    Transport.cpp)

harmonia_configure_target(harmonia_audio)
target_link_libraries(harmonia_audio PUBLIC harmonia_core)
