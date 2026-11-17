# carthage-scan: loads one plugin in its own process and reports what it found (F9).
juce_add_console_app(carthage_scan PRODUCT_NAME "carthage-scan")
target_sources(carthage_scan PRIVATE ScanMain.cpp)
target_link_libraries(carthage_scan PRIVATE carthage_engine carthage_warnings)
