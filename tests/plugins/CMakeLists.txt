# Two deliberately broken VST3 bundles for the scanner's tests: one crashes when loaded, one hangs.
# macOS only (a bundle with a Mach-O binary); built into build/<preset>/test-plugins, never installed.
if(NOT APPLE)
    return()
endif()

set(CARTHAGE_TEST_PLUGIN_DIR "${CMAKE_BINARY_DIR}/test-plugins" CACHE INTERNAL "Folder of the broken test plugins")

foreach(kind Crash Hang)
    set(target carthage_test_plugin_${kind})
    add_library(${target} MODULE TestPlugin.c)
    target_compile_definitions(${target} PRIVATE CARTHAGE_TEST_HANG=$<STREQUAL:${kind},Hang>)
    set_target_properties(${target} PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION vst3
        OUTPUT_NAME "Carthage Test ${kind}"
        LIBRARY_OUTPUT_DIRECTORY "${CARTHAGE_TEST_PLUGIN_DIR}")
endforeach()
