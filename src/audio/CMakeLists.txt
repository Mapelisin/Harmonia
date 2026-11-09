add_library(harmonia_audio STATIC
    AudioInputs.h
    AudioInputs.cpp
    Automation.h
    Automation.cpp
    DeviceStatus.h
    DeviceStatus.cpp
    DeviceSetup.h
    DeviceSetup.cpp
    Effects.h
    Effects.cpp
    Export.h
    Export.cpp
    ExportFormat.h
    ExportFormat.cpp
    Mp3Encoder.h
    FilePreview.h
    FilePreview.cpp
    MidiRender.h
    MidiRender.cpp
    Loudness.h
    Loudness.cpp
    MixCheck.h
    MixCheck.cpp
    Limiter.h
    Limiter.cpp
    Pump.h
    Pump.cpp
    DrumKit.h
    DrumKit.cpp
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
    PluginSafety.h
    PluginSafety.cpp
    Presets.h
    Presets.cpp
    Groups.h
    Groups.cpp
    Sidechain.h
    Sidechain.cpp
    Sends.h
    Sends.cpp
    Transport.h
    Transport.cpp)

if (WIN32)
    target_sources(harmonia_audio PRIVATE Mp3EncoderWindows.cpp)
else()
    message(FATAL_ERROR "MP3 export (src/audio/Mp3Encoder.h) has no implementation for this system yet")
endif()

harmonia_configure_target(harmonia_audio)
target_link_libraries(harmonia_audio PUBLIC harmonia_core)
