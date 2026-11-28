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
    MetronomeTests.cpp
    LogTests.cpp
    OverviewCacheTests.cpp
    SettingsTestTests.cpp
    SampleEditTests.cpp
    MotionTests.cpp
    MixerTests.cpp
    ExportTests.cpp
    DelayCompensationTests.cpp
    TakePlanTests.cpp
    OutputGainTests.cpp
    PluginHostTests.cpp)
target_include_directories(carthage_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(carthage_tests PRIVATE CARTHAGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CARTHAGE_SCANNER_PATH="$<TARGET_FILE:carthage_scan>"
    CARTHAGE_PLUGHOST_PATH="$<TARGET_FILE:carthage_plughost>")
add_dependencies(carthage_tests carthage_scan carthage_plughost)
if(APPLE)
    target_compile_definitions(carthage_tests PRIVATE CARTHAGE_TEST_PLUGIN_DIR="${CARTHAGE_TEST_PLUGIN_DIR}")
    add_dependencies(carthage_tests carthage_test_plugin_Crash carthage_test_plugin_Hang)
endif()
target_link_libraries(carthage_tests PRIVATE carthage_engine carthage_ui carthage_warnings)

add_test(NAME unit COMMAND carthage_tests)
# The whole output of a failing run, not CTest's first few kB: CI's log is all there is to read.
file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
     "set(CTEST_CUSTOM_MAXIMUM_FAILED_TEST_OUTPUT_SIZE 4000000)\nset(CTEST_CUSTOM_MAXIMUM_PASSED_TEST_OUTPUT_SIZE 4000000)\n")
add_test(NAME no-shared-mutex COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/NoSharedMutex.cmake)
