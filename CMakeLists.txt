cmake_minimum_required(VERSION 3.30)

# The version comes from the release tag in CI (-DCARTHAGE_VERSION=1.2.3). Local builds are 0.0.0.
set(CARTHAGE_VERSION "0.0.0" CACHE STRING "Carthage version, injected from the release tag")
set(CARTHAGE_BUILD_NUMBER "0" CACHE STRING "Monotonic build number, injected by the release workflow")

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
    # Windows 10 1607 (DECISIONS O8)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000002)
endif()

option(CARTHAGE_RTSAN "Build with Clang's RealTimeSanitizer (needs a Clang that supports it)" OFF)
option(CARTHAGE_TESTS "Build the unit and engine tests" ON)

include(cmake/CarthageOptions.cmake)
add_compile_definitions(CARTHAGE_VERSION_STRING="${CARTHAGE_VERSION}")

# ---- Dependencies (git submodules, pinned: DECISIONS O7) -------------------------------------------
add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(external/tracktion_engine/modules EXCLUDE_FROM_ALL)

# ---- Carthage ------------------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(tools/bench)
add_subdirectory(tools/snapshot)

if(CARTHAGE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
