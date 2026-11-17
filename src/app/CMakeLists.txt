# The Carthage application: app/ (windows, commands, paths) and ui/ (components, theme).
juce_add_gui_app(Carthage
    PRODUCT_NAME "Carthage"
    VERSION "${CARTHAGE_VERSION}"
    BUILD_VERSION "${CARTHAGE_BUILD_NUMBER}"
    BUNDLE_ID "io.github.jonnenieminen.carthage"
    COMPANY_NAME "Jonne Nieminen"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Carthage records and monitors the instruments plugged into your audio interface."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(Carthage PRIVATE
    Main.cpp
    Commands.h
    Paths.cpp Paths.h
    ../ui/Controls.cpp ../ui/Controls.h
    ../ui/MainComponent.cpp ../ui/MainComponent.h
    ../ui/SettingsPanel.cpp ../ui/SettingsPanel.h
    ../ui/TakeLane.cpp ../ui/TakeLane.h
    ../ui/theme/LookAndFeel.cpp ../ui/theme/LookAndFeel.h
    ../ui/theme/Theme.h)

target_include_directories(Carthage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Carthage PRIVATE carthage_engine carthage_warnings)

# carthage-scan ships next to the app's executable (app::Paths::scanner()).
add_dependencies(Carthage carthage_scan)
add_custom_command(TARGET Carthage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:carthage_scan>
            $<TARGET_FILE_DIR:Carthage>/carthage-scan$<$<PLATFORM_ID:Windows>:.exe>
    COMMENT "Placing carthage-scan next to the app")
