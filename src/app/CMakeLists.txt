juce_add_gui_app(Harmonia
    PRODUCT_NAME "Harmonia"
    COMPANY_NAME "Harmonia"
    BUNDLE_ID "org.harmonia.app"
    VERSION "${PROJECT_VERSION}"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(Harmonia PRIVATE
    Main.cpp
    MainWindow.h
    MainWindow.cpp)

harmonia_configure_target(Harmonia)
target_link_libraries(Harmonia PRIVATE harmonia_ui)
target_compile_definitions(Harmonia PRIVATE HARMONIA_VERSION_STRING="${PROJECT_VERSION}")
