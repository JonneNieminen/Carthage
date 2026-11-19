# Unit and engine tests: headless, no audio hardware, no third-party plugins (docs/TESTING.md).
add_subdirectory(plugins)   # broken test plugins for the scanner (macOS)
juce_add_console_app(carthage_tests PRODUCT_NAME "carthage-tests")
target_sources(carthage_tests PRIVATE
    TestMain.cpp
    EditingTests.cpp
    GridTests.cpp
    LatencyProbeTests.cpp
    PluginLibraryTests.cpp
    PluginScanTests.cpp
    ProjectTests.cpp
    SampleTests.cpp
    TakePlacementTests.cpp
    TakeRecorderTests.cpp
    ThemeTests.cpp
    MetronomeTests.cpp)
target_include_directories(carthage_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(carthage_tests PRIVATE CARTHAGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CARTHAGE_SCANNER_PATH="$<TARGET_FILE:carthage_scan>")
add_dependencies(carthage_tests carthage_scan)
if(APPLE)
    target_compile_definitions(carthage_tests PRIVATE CARTHAGE_TEST_PLUGIN_DIR="${CARTHAGE_TEST_PLUGIN_DIR}")
    add_dependencies(carthage_tests carthage_test_plugin_Crash carthage_test_plugin_Hang)
endif()
target_link_libraries(carthage_tests PRIVATE carthage_engine carthage_warnings)

add_test(NAME unit COMMAND carthage_tests)
add_test(NAME no-shared-mutex COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/NoSharedMutex.cmake)
