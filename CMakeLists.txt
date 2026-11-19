cmake_minimum_required(VERSION 3.30)

# The version comes from the release tag (docs/RELEASING.md, "Versions and tags"): the release
# workflow passes -DCARTHAGE_RELEASE=1.2.3 or 1.2.3-beta.4 (the tag without its "v"). Local and CI
# builds leave it empty and are 0.0.0-dev, build 0, which every release is newer than.
include(cmake/CarthageVersion.cmake)

# Minimum OS versions (DECISIONS O8). Set before project() so every target, JUCE included, inherits them.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Apple Silicon native")
endif()

project(Carthage VERSION ${CARTHAGE_VERSION} LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

# C++ level (DECISIONS O2): the newest standard both toolchains and both libraries accept.
# MSVC has no C++26 mode CMake can name; its C++23 setting is /std:c++latest, the newest it offers
# (DECISIONS W11).
if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # clangd reads build/<preset>/compile_commands.json

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # The minimum Windows (10 1607, DECISIONS W8) is enforced by the installer, not by _WIN32_WINNT
    # or NTDDI_VERSION: setting those hides SDK interfaces JUCE uses behind runtime checks
    # (IDWriteFontFace5 and friends), and JUCE's Windows code then does not compile.
endif()

option(CARTHAGE_RTSAN "Build with Clang's RealTimeSanitizer (needs a Clang that supports it)" OFF)
option(CARTHAGE_ASAN "Build with AddressSanitizer and UndefinedBehaviorSanitizer (Clang or GCC)" OFF)
option(CARTHAGE_TESTS "Build the unit and engine tests" ON)

include(cmake/CarthageOptions.cmake)
# The version reaches Carthage's own code only (through carthage_warnings, which every Carthage
# target links), never JUCE or Tracktion: a new version must not recompile the libraries, or the
# compiler cache in CI would miss on every release.
target_compile_definitions(carthage_warnings INTERFACE
    CARTHAGE_VERSION_STRING="${CARTHAGE_VERSION_STRING}" CARTHAGE_BUILD_NUMBER=${CARTHAGE_BUILD_NUMBER})

# ---- Dependencies (git submodules, pinned: DECISIONS O7) -------------------------------------------
add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(external/tracktion_engine/modules EXCLUDE_FROM_ALL)
include(cmake/Updaters.cmake)   # Sparkle / WinSparkle release archives, pinned by SHA-256

# ---- Carthage ------------------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(tools/bench)
add_subdirectory(tools/snapshot)

if(CARTHAGE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
