# carthage-plughost: runs plugins in a process of its own, for the app (5f, W65).
juce_add_console_app(carthage_plughost PRODUCT_NAME "carthage-plughost")
target_sources(carthage_plughost PRIVATE
    PlugHostMain.cpp
    PluginWindow.cpp PluginWindow.h
    $<IF:$<PLATFORM_ID:Darwin>,platform/HostApp_mac.mm,platform/HostApp_other.cpp>)
if(APPLE)
    set_source_files_properties(platform/HostApp_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(carthage_plughost PRIVATE "-framework AppKit")
endif()
target_link_libraries(carthage_plughost PRIVATE carthage_engine carthage_warnings)
