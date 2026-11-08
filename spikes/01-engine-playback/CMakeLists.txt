# Spike 01: verify that JUCE + Tracktion Engine build on our toolchain, play a MIDI
# chord progression through a built-in synth or a VST3 instrument via ASIO/WASAPI,
# and render offline. Throwaway code: findings go to docs/research/spike-01.md.

cmake_minimum_required(VERSION 3.22)
project(HarmoniaSpike01 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(HARMONIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TE_ROOT "${HARMONIA_ROOT}/external/tracktion_engine")

add_subdirectory("${TE_ROOT}/modules/juce" "${CMAKE_BINARY_DIR}/juce")
add_subdirectory("${TE_ROOT}/modules" "${CMAKE_BINARY_DIR}/tracktion")

juce_add_gui_app(HarmoniaSpike01
    PRODUCT_NAME "Harmonia Spike 01"
    COMPANY_NAME "Harmonia"
    BUNDLE_ID "org.harmonia.spike01"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLUGINHOST_AU TRUE)

target_sources(HarmoniaSpike01 PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/EngineSession.cpp
    Source/AudioDevice.cpp
    Source/PluginScanner.cpp
    Source/PluginWindow.cpp
    Source/SelfTest.cpp)

target_compile_definitions(HarmoniaSpike01 PRIVATE
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HarmoniaSpike01,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:HarmoniaSpike01,JUCE_VERSION>")

target_link_libraries(HarmoniaSpike01 PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
