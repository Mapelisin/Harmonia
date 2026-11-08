add_library(harmonia_ui STATIC
    Theme.h
    Theme.cpp
    RenderingPolicy.h
    RenderingPolicy.cpp
    StatusBar.h
    StatusBar.cpp
    PluginWindow.h
    PluginWindow.cpp
    TransportBar.h
    TransportBar.cpp
    InstrumentPanel.h
    InstrumentPanel.cpp
    PreviewKeyboard.h
    PreviewKeyboard.cpp
    MainComponent.h
    MainComponent.cpp)

harmonia_configure_target(harmonia_ui)
target_link_libraries(harmonia_ui PUBLIC harmonia_audio)
