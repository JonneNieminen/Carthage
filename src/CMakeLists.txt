# ---- carthage_juce: JUCE + Tracktion Engine compiled once ----------------------------------------
# JUCE modules are INTERFACE libraries whose sources compile into each target that links them. The
# app, the scanner and the tests all need them, so they are compiled once into this static library and
# its include paths and definitions are re-exported to everything that links it.
add_library(carthage_juce STATIC)

target_link_libraries(carthage_juce
    PRIVATE
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        tracktion::tracktion_core
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_animation   # motion that answers an action (W45), synced to the display
        juce::juce_dsp
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        carthage_sanitizers)

target_compile_definitions(carthage_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_ARA=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_FLAC=1
        # The sample browser reads MP3 and Ogg too (W30): JUCE's own decoders, no new dependency.
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_OGGVORBIS=1
        # The ASIO SDK ships inside JUCE under GPLv3 (JUCE 8.0.11+), compatible with AGPLv3 (DECISIONS W9)
        $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
        $<$<PLATFORM_ID:Windows>:JUCE_WASAPI=1>
        # No stock plugins (F4): Tracktion's Airwindows effects stay compiled out.
        TRACKTION_AIR_WINDOWS=0
        # Time-stretching (DECISIONS W36): Signalsmith Stretch, MIT, header-only, shipped inside the
        # pinned Tracktion. The stretch tool keeps pitch, and loops can follow the project tempo.
        TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1
        TRACKTION_ENABLE_ARA=0
        TRACKTION_ENABLE_SINGLETONS=0
        TRACKTION_LOG_DEVICES=0
        TRACKTION_UNIT_TESTS=0)

# SYSTEM: third-party headers must not trip Carthage's warnings-as-errors.
target_include_directories(carthage_juce SYSTEM INTERFACE
    $<TARGET_PROPERTY:carthage_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(carthage_juce INTERFACE
    $<TARGET_PROPERTY:carthage_juce,COMPILE_DEFINITIONS>)

# JUCE's bundled VST3 SDK uses std::wstring_convert, which libc++ removes in C++26, so the
# third-party sources compile at C++23; Carthage's own code uses C++26 (DECISIONS W11).
set_target_properties(carthage_juce PROPERTIES
    CXX_STANDARD 23
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt)
    add_subdirectory(engine)
    add_subdirectory(ui)
    add_subdirectory(scan)
    add_subdirectory(app)
endif()
