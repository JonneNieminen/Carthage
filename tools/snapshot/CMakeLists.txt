# carthage-snapshot: renders the app's window content to PNG in fixed states (see SnapshotMain.cpp).
juce_add_console_app(carthage_snapshot PRODUCT_NAME "carthage-snapshot")
target_sources(carthage_snapshot PRIVATE SnapshotMain.cpp)
target_link_libraries(carthage_snapshot PRIVATE carthage_ui carthage_warnings)
add_dependencies(carthage_snapshot carthage_scan)
add_custom_command(TARGET carthage_snapshot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:carthage_scan>
            $<TARGET_FILE_DIR:carthage_snapshot>/carthage-scan$<$<PLATFORM_ID:Windows>:.exe>)
