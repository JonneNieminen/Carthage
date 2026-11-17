# carthage-bench: hardware measurements through the app's own engine (docs/TESTING.md, "Measuring").
juce_add_console_app(carthage_bench PRODUCT_NAME "carthage-bench")
target_sources(carthage_bench PRIVATE BenchMain.cpp)

# Counts os_workgroup_join calls. A shared library, because dyld applies the __interpose section of
# a loaded library, not of the executable itself (WorkgroupJoins.cpp). Bench-only; never in the app.
add_library(carthage_bench_workgroups SHARED WorkgroupJoins.cpp WorkgroupJoins.h)
target_link_libraries(carthage_bench_workgroups PRIVATE carthage_warnings)
target_link_libraries(carthage_bench PRIVATE carthage_bench_workgroups)
target_link_libraries(carthage_bench PRIVATE carthage_engine carthage_warnings)
add_dependencies(carthage_bench carthage_scan)
add_custom_command(TARGET carthage_bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:carthage_scan>
            $<TARGET_FILE_DIR:carthage_bench>/carthage-scan$<$<PLATFORM_ID:Windows>:.exe>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:carthage_bench_workgroups>
            $<TARGET_FILE_DIR:carthage_bench>/)
