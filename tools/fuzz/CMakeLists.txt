# Fuzz targets (W38): libFuzzer, from Homebrew LLVM, with AddressSanitizer. Built only by the `fuzz`
# preset (CARTHAGE_FUZZ); run as docs/TESTING.md says, and in CI for a minute each.
foreach(target project audio)
    string(SUBSTRING ${target} 0 1 first)
    string(TOUPPER ${first} first)
    string(SUBSTRING ${target} 1 -1 rest)
    juce_add_console_app(carthage_fuzz_${target} PRODUCT_NAME "carthage-fuzz-${target}")
    target_sources(carthage_fuzz_${target} PRIVATE Fuzz${first}${rest}.cpp)
    target_include_directories(carthage_fuzz_${target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(carthage_fuzz_${target} PRIVATE carthage_engine carthage_warnings)
    target_link_options(carthage_fuzz_${target} PRIVATE -fsanitize=fuzzer)
endforeach()
