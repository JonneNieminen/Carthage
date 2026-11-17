# Unit and engine tests: headless, no audio hardware, no third-party plugins (docs/TESTING.md).
juce_add_console_app(carthage_tests PRODUCT_NAME "carthage-tests")
target_sources(carthage_tests PRIVATE
    TestMain.cpp
    LatencyProbeTests.cpp
    PluginScanTests.cpp
    TakeRecorderTests.cpp
    ThemeTests.cpp)
target_include_directories(carthage_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(carthage_tests PRIVATE CARTHAGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(carthage_tests PRIVATE carthage_engine carthage_warnings)

add_test(NAME unit COMMAND carthage_tests)
add_test(NAME no-shared-mutex COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/NoSharedMutex.cmake)
