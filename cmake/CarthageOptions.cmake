# Shared compile settings for every Carthage target.

# Warnings are errors for Carthage's own code (AGENTS.md, "Code"). Third-party module sources are
# compiled inside carthage_juce, which gets the warning flags without -Werror: we do not patch
# submodules, so a warning there must not break the build.
add_library(carthage_warnings INTERFACE)
if(MSVC)
    target_compile_options(carthage_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(carthage_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor
        -Wold-style-cast -Woverloaded-virtual -Wnull-dereference -Werror)
endif()

# RealTimeSanitizer: functions marked [[clang::nonblocking]], and every scope entered with
# carthage::rt::ScopedRealtime, abort with a stack trace on any allocation, lock or blocking call.
add_library(carthage_sanitizers INTERFACE)
if(CARTHAGE_RTSAN)
    target_compile_options(carthage_sanitizers INTERFACE -fsanitize=realtime)
    target_link_options(carthage_sanitizers INTERFACE -fsanitize=realtime)
    target_compile_definitions(carthage_sanitizers INTERFACE CARTHAGE_RTSAN=1)
endif()
