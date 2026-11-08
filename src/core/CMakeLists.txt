add_library(harmonia_core STATIC
    Tracktion.h
    Session.h
    Session.cpp
    Project.h
    Project.cpp)

harmonia_configure_target(harmonia_core)
target_link_libraries(harmonia_core PUBLIC harmonia_juce)
