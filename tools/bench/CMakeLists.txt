# carthage-bench: hardware measurements through the app's own engine (docs/TESTING.md, "Measuring").
juce_add_console_app(carthage_bench PRODUCT_NAME "carthage-bench")
target_sources(carthage_bench PRIVATE BenchMain.cpp)
target_link_libraries(carthage_bench PRIVATE carthage_engine carthage_warnings)
add_dependencies(carthage_bench carthage_scan)
add_custom_command(TARGET carthage_bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:carthage_scan>
            $<TARGET_FILE_DIR:carthage_bench>/carthage-scan$<$<PLATFORM_ID:Windows>:.exe>)
