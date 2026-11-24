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

target_include_directories(carthage_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(carthage_engine PUBLIC carthage_juce PRIVATE carthage_warnings)
