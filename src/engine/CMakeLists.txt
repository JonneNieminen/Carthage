# The engine layer: no UI, no app (AGENTS.md "Engine"). Runs headless in the tests.
add_library(carthage_engine STATIC
    AudioCallback.cpp AudioCallback.h
    LatencyProbe.cpp LatencyProbe.h
    PluginLibrary.cpp PluginLibrary.h
    time/Grid.cpp time/Grid.h time/TapTempo.h
    PluginScan.cpp PluginScan.h
    RealtimeCheck.h
    Editing.cpp Editing.h
    Export.cpp Export.h
    Mixer.cpp Mixer.h
    Presets.cpp Presets.h
    plughost/Link.cpp plughost/Link.h
    plughost/Protocol.cpp plughost/Protocol.h
    plughost/HostProcess.cpp plughost/HostProcess.h
    plughost/PluginProxy.cpp plughost/PluginProxy.h
    plughost/Processes.cpp plughost/Processes.h
    plughost/TestPlugins.cpp plughost/TestPlugins.h
    platform/SharedMemory.h
    SampleEdits.cpp SampleEdits.h
    FormantPlugin.cpp FormantPlugin.h
    Project.cpp Project.h
    Autosave.cpp Autosave.h
    SampleAnalysis.cpp SampleAnalysis.h
    SampleLibrary.cpp SampleLibrary.h
    SamplePreview.cpp SamplePreview.h
    Metronome.cpp Metronome.h
    Log.cpp Log.h
    Trace.cpp Trace.h
    SettingsTest.cpp SettingsTest.h
    AudioFormats.h
    Session.cpp Session.h
    TakePlacement.h
    TakePlan.cpp TakePlan.h
    TakeRecorder.cpp TakeRecorder.h)

# Plugins in their own processes (5f, W65): the only OS calls are in these files.
if(WIN32)
    target_sources(carthage_engine PRIVATE platform/SharedMemory_win.cpp)
else()
    target_sources(carthage_engine PRIVATE platform/SharedMemory_posix.cpp)
endif()

target_include_directories(carthage_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(carthage_engine PUBLIC carthage_juce PRIVATE carthage_warnings)
