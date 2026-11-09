cmake_minimum_required(VERSION 3.22)

project(Harmonia VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (MSVC)
    # Static runtime: the app runs without the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(HARMONIA_BUILD_TESTS "Build the test suite" ON)

set(HARMONIA_TE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/tracktion_engine")

if (NOT EXISTS "${HARMONIA_TE_DIR}/modules/juce/CMakeLists.txt")
    message(FATAL_ERROR "Submodules are missing. Run:\n"
        "  git -c url.\"https://github.com/\".insteadOf=\"git@github.com:\" submodule update --init --recursive")
endif()

include(cmake/HarmoniaEnginePatches.cmake)

add_subdirectory("${HARMONIA_TE_DIR}/modules/juce" "${CMAKE_BINARY_DIR}/external/juce" EXCLUDE_FROM_ALL)
add_subdirectory("${HARMONIA_TE_DIR}/modules" "${CMAKE_BINARY_DIR}/external/tracktion" EXCLUDE_FROM_ALL)

include(cmake/HarmoniaJuce.cmake)
include(cmake/HarmoniaTarget.cmake)

add_subdirectory(src/sandbox)
add_subdirectory(src/core)
add_subdirectory(src/audio)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if (HARMONIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
