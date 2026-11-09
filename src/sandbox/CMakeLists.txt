# Plugins in a process of their own (both ends: Harmonia's stand-in and the plugin's process).
add_library(harmonia_sandbox STATIC
    Platform.h
    Protocol.h
    Protocol.cpp
    Channel.h
    Channel.cpp
    Host.h
    Host.cpp
    RemotePlugin.h
    RemotePlugin.cpp
    Sandbox.h
    SandboxShared.h
    Sandbox.cpp)

if (WIN32)
    target_sources(harmonia_sandbox PRIVATE PlatformWindows.cpp)
else()
    message(FATAL_ERROR "The plugin sandbox (src/sandbox/Platform.h) has no implementation for this system yet")
endif()

harmonia_configure_target(harmonia_sandbox)
target_link_libraries(harmonia_sandbox PUBLIC harmonia_juce)
