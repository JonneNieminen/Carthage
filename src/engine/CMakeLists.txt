# The engine layer: no UI, no app (AGENTS.md "Engine"). Runs headless in the tests.
add_library(carthage_engine STATIC
    AudioCallback.cpp AudioCallback.h
    LatencyProbe.cpp LatencyProbe.h
    PluginScan.cpp PluginScan.h
    RealtimeCheck.h
    Session.cpp Session.h
    TakePlacement.h
    TakeRecorder.cpp TakeRecorder.h)

target_include_directories(carthage_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(carthage_engine PUBLIC carthage_juce PRIVATE carthage_warnings)
