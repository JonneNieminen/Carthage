# The components and the theme. Linked by the app and by carthage-snapshot.
add_library(carthage_ui STATIC
    Controls.cpp Controls.h
    MainComponent.cpp MainComponent.h
    PluginBrowser.cpp PluginBrowser.h
    SettingsPanel.cpp SettingsPanel.h
    ShortcutEditor.cpp ShortcutEditor.h
    ProjectTabs.cpp ProjectTabs.h
    Timeline.cpp Timeline.h
    UpdateBanner.cpp UpdateBanner.h
    theme/LookAndFeel.cpp theme/LookAndFeel.h
    theme/Theme.h)

target_include_directories(carthage_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(carthage_ui PUBLIC carthage_engine PRIVATE carthage_warnings)
