# Spike 02: how fast can JUCE 9 draw a piano roll (grid + keyboard + many notes)
# with the Direct2D and software renderers? Findings: docs/research/spike-02.md.

cmake_minimum_required(VERSION 3.22)
project(HarmoniaSpike02 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(HARMONIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_subdirectory("${HARMONIA_ROOT}/external/tracktion_engine/modules/juce" "${CMAKE_BINARY_DIR}/juce")

juce_add_gui_app(HarmoniaSpike02
    PRODUCT_NAME "Harmonia Spike 02"
    COMPANY_NAME "Harmonia"
    BUNDLE_ID "org.harmonia.spike02")

target_sources(HarmoniaSpike02 PRIVATE
    Source/Main.cpp
    Source/NoteSet.cpp
    Source/PianoRollView.cpp
    Source/Benchmark.cpp)

target_compile_definitions(HarmoniaSpike02 PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HarmoniaSpike02,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:HarmoniaSpike02,JUCE_VERSION>")

target_link_libraries(HarmoniaSpike02 PRIVATE
    juce::juce_gui_basics
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
