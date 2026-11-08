add_executable(harmonia_tests
    TestMain.cpp
    TestSession.h
    TestAudioFiles.h
    TestAudioRig.h
    core/TestProject.h
    core/ProjectTests.cpp
    core/NoteTests.cpp
    core/NoteEditorTests.cpp
    core/ClipLinksTests.cpp
    core/ControllersTests.cpp
    core/SnapTests.cpp
    core/NoteOpsTests.cpp
    core/NoteRecorderTests.cpp
    core/ClipsTests.cpp
    core/TempoTests.cpp
    core/MidiFilesTests.cpp
    core/AutosaveTests.cpp
    core/NoteToolsTests.cpp
    core/ScalesTests.cpp
    core/ProgressionsTests.cpp
    core/DrumPatternsTests.cpp
    core/BassLinesTests.cpp
    core/MelodiesTests.cpp
    core/SectionsTests.cpp
    core/SongFormsTests.cpp
    core/ClipLoopTests.cpp
    audio/DeviceStatusTests.cpp
    audio/AudioTests.cpp
    audio/EffectsTests.cpp
    audio/RecordingTests.cpp
    audio/SendsTests.cpp
    audio/AutomationTests.cpp
    audio/StemsTests.cpp
    audio/MidiRenderTests.cpp
    audio/LoudnessTests.cpp
    audio/LimiterTests.cpp
    audio/PumpTests.cpp
    audio/DrumKitTests.cpp
    audio/MixCheckTests.cpp
    audio/AudioTempoTests.cpp
    ui/PianoRollTests.cpp
    ui/PianoRollViewTests.cpp
    ui/NoteInputTests.cpp
    ui/ArrangementTests.cpp
    ui/MixerTests.cpp
    ui/AudioClipTests.cpp
    ui/ClipEditTests.cpp
    ui/AutomationPanelTests.cpp
    ui/ClipFadeTests.cpp
    ui/TempoUiTests.cpp
    ui/MidiImportTests.cpp
    ui/TrackOpsTests.cpp
    ui/NoteCommandsTests.cpp
    ui/NoteToolWindowTests.cpp
    ui/ScaleChordTests.cpp
    ui/TrackColourTests.cpp
    ui/ClipLoopUiTests.cpp
    ui/MarkerTests.cpp
    ui/BarsEditingTests.cpp
    ui/ShortcutsPanelTests.cpp
    ui/PianoRollRulerTests.cpp
    ui/SoundBrowserTests.cpp
    ui/RecentProjectsTests.cpp
    ui/DrumNamesTests.cpp
    ui/DrumTrackTests.cpp
    ui/AudioClipToolsTests.cpp
    ui/AudioTempoUiTests.cpp
    ui/ChordNameTests.cpp
    ui/LoopExportTests.cpp
    ui/MainMenuTests.cpp
    ui/MixDoctorMenuTests.cpp
    ui/ChordLaneTests.cpp
    ui/ControllerLaneTests.cpp)

harmonia_configure_target(harmonia_tests)
target_link_libraries(harmonia_tests PRIVATE harmonia_ui)
target_include_directories(harmonia_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(harmonia_tests SYSTEM PRIVATE "${HARMONIA_TE_DIR}/modules/3rd_party/doctest")

# doctest forward-declares std types by default, which new MSVC versions warn about.
target_compile_definitions(harmonia_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

# Each test's time is printed, so if the run ever hangs the output (shown on the timeout) says where.
add_test(NAME harmonia_tests COMMAND harmonia_tests --duration=true)
set_tests_properties(harmonia_tests PROPERTIES TIMEOUT 900)
