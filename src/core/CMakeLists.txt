add_library(harmonia_core STATIC
    Tracktion.h
    Session.h
    Session.cpp
    Project.h
    Project.cpp
    Note.h
    Note.cpp
    NoteEditor.h
    NoteEditor.cpp
    ClipLinks.h
    ClipLinks.cpp
    Clips.h
    Clips.cpp
    Tracks.h
    Tracks.cpp
    Snap.h
    Snap.cpp
    NoteOps.h
    NoteOps.cpp
    NoteRecorder.h
    NoteRecorder.cpp)

harmonia_configure_target(harmonia_core)
target_link_libraries(harmonia_core PUBLIC harmonia_juce)
