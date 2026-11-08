add_executable(harmonia_tests
    TestMain.cpp
    TestSession.h
    core/TestProject.h
    core/ProjectTests.cpp
    core/NoteTests.cpp
    core/NoteEditorTests.cpp
    core/ClipLinksTests.cpp
    core/SnapTests.cpp
    core/NoteOpsTests.cpp
    core/NoteRecorderTests.cpp
    audio/DeviceStatusTests.cpp
    audio/AudioTests.cpp
    ui/PianoRollTests.cpp
    ui/PianoRollViewTests.cpp
    ui/NoteInputTests.cpp)

harmonia_configure_target(harmonia_tests)
target_link_libraries(harmonia_tests PRIVATE harmonia_ui)
target_include_directories(harmonia_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(harmonia_tests SYSTEM PRIVATE "${HARMONIA_TE_DIR}/modules/3rd_party/doctest")

# doctest forward-declares std types by default, which new MSVC versions warn about.
target_compile_definitions(harmonia_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

add_test(NAME harmonia_tests COMMAND harmonia_tests)
