# The components and the theme. Linked by the app and by carthage-snapshot.
add_library(carthage_ui STATIC
    Controls.cpp Controls.h
    MainComponent.cpp MainComponent.h
    PluginBrowser.cpp PluginBrowser.h
    SettingsPanel.cpp SettingsPanel.h
    ShortcutEditor.cpp ShortcutEditor.h
    ProjectTabs.cpp ProjectTabs.h
    SampleBrowser.cpp SampleBrowser.h
    SampleEditor.cpp SampleEditor.h
    StartScreen.cpp StartScreen.h
    CommandPalette.cpp CommandPalette.h
    Timeline.cpp Timeline.h
    UpdateBanner.cpp UpdateBanner.h
    theme/LookAndFeel.cpp theme/LookAndFeel.h
    theme/Fonts.cpp
    theme/Theme.h)

# The bundled typefaces (W45): Barlow, Barlow Semi Condensed and IBM Plex Mono, all SIL OFL; their
# licences are beside them in resources/fonts and ship with the app.
juce_add_binary_data(carthage_fonts HEADER_NAME CarthageFonts.h NAMESPACE CarthageFonts
    SOURCES
        ${CMAKE_SOURCE_DIR}/resources/fonts/Barlow-Regular.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/Barlow-SemiBold.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/BarlowSemiCondensed-Regular.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/BarlowSemiCondensed-SemiBold.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/IBMPlexMono-Regular.ttf)

target_include_directories(carthage_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(carthage_ui PUBLIC carthage_engine PRIVATE carthage_warnings carthage_fonts)
