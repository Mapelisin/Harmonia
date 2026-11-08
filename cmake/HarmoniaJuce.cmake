# Compiles JUCE and Tracktion Engine exactly once into a static library that every
# Harmonia module links. JUCE modules are CMake INTERFACE libraries whose sources would
# otherwise be compiled into each consumer; linking them PRIVATE here and re-exporting
# their include paths and definitions avoids duplicate symbols and repeated compile time.

add_library(harmonia_juce STATIC)

target_link_libraries(harmonia_juce
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_graph
        tracktion::tracktion_engine
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags)

target_compile_definitions(harmonia_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

# Re-export what the PRIVATE JUCE modules contribute, so consumers see the same configuration.
target_include_directories(harmonia_juce
    INTERFACE $<TARGET_PROPERTY:harmonia_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(harmonia_juce
    INTERFACE $<TARGET_PROPERTY:harmonia_juce,COMPILE_DEFINITIONS>)

set_target_properties(harmonia_juce PROPERTIES FOLDER "External")
