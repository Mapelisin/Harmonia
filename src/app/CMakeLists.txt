juce_add_gui_app(Harmonia
    PRODUCT_NAME "Harmonia"
    COMPANY_NAME "Harmonia"
    BUNDLE_ID "org.harmonia.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.svg"
    VERSION "${PROJECT_VERSION}"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

# The exe's version (Explorer, the installer) is in a resource file JUCE remakes only when the icon
# changes: a new version in CMakeLists.txt must remake it too.
if(WIN32)
    get_target_property(harmonia_info_file Harmonia JUCE_INFO_FILE)
    get_target_property(harmonia_juce_code Harmonia JUCE_GENERATED_SOURCES_DIRECTORY)
    add_custom_command(OUTPUT "${harmonia_juce_code}/Harmonia_resources.rc" APPEND DEPENDS "${harmonia_info_file}")
endif()

target_sources(Harmonia PRIVATE
    Main.cpp
    MainWindow.h
    MainWindow.cpp
    SelfTest.h
    SelfTest.cpp)

harmonia_configure_target(Harmonia)
target_link_libraries(Harmonia PRIVATE harmonia_ui)
target_compile_definitions(Harmonia PRIVATE HARMONIA_VERSION_STRING="${PROJECT_VERSION}")
