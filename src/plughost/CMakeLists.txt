# carthage-plughost: runs plugins in a process of its own, for the app (5f, W65).
juce_add_console_app(carthage_plughost PRODUCT_NAME "carthage-plughost")
target_sources(carthage_plughost PRIVATE PlugHostMain.cpp)
target_link_libraries(carthage_plughost PRIVATE carthage_engine carthage_warnings)
