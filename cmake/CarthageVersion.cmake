# Turns the release tag into the three version values the app and its bundles need.
#
#   CARTHAGE_VERSION         1.2.3          numeric; project(), Info.plist short version, Windows resources
#   CARTHAGE_VERSION_STRING  1.2.3-beta.4   what the app shows and the appcast names
#   CARTHAGE_BUILD_NUMBER    1020304        CFBundleVersion and WinSparkle's build version
#
# Sparkle and WinSparkle compare the build number, so it must grow with every release, betas
# included, and a beta must sort before its own stable release:
#
#   build = major * 1000000 + minor * 10000 + patch * 100 + (beta N ? N : 99)
#
#   0.1.0 -> 10099    0.1.1-beta.1 -> 10101    0.1.1-beta.2 -> 10102    0.1.1 -> 10199
#
# So minor and patch stay below 100 and a version has at most 98 betas. Only "-beta.N" suffixes
# are accepted; anything else stops the configure step instead of producing a build number that
# could sort wrongly.

set(CARTHAGE_RELEASE "" CACHE STRING "Release version from the tag without its v (1.2.3 or 1.2.3-beta.4); empty for local builds")

if(CARTHAGE_RELEASE STREQUAL "")
    set(CARTHAGE_VERSION "0.0.0")
    set(CARTHAGE_VERSION_STRING "0.0.0-dev")
    set(CARTHAGE_BUILD_NUMBER 0)
    return()
endif()

if(NOT CARTHAGE_RELEASE MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-beta\\.([0-9]+))?$")
    message(FATAL_ERROR "CARTHAGE_RELEASE '${CARTHAGE_RELEASE}' is not X.Y.Z or X.Y.Z-beta.N (docs/RELEASING.md)")
endif()

set(_major ${CMAKE_MATCH_1})
set(_minor ${CMAKE_MATCH_2})
set(_patch ${CMAKE_MATCH_3})
set(_beta "${CMAKE_MATCH_5}")

if(_minor GREATER 99 OR _patch GREATER 99)
    message(FATAL_ERROR "CARTHAGE_RELEASE '${CARTHAGE_RELEASE}': minor and patch must be below 100 for the build number scheme")
endif()

if(_beta STREQUAL "")
    set(_step 99)
elseif(_beta GREATER_EQUAL 1 AND _beta LESS_EQUAL 98)
    set(_step ${_beta})
else()
    message(FATAL_ERROR "CARTHAGE_RELEASE '${CARTHAGE_RELEASE}': beta numbers run from 1 to 98")
endif()

set(CARTHAGE_VERSION "${_major}.${_minor}.${_patch}")
set(CARTHAGE_VERSION_STRING "${CARTHAGE_RELEASE}")
math(EXPR CARTHAGE_BUILD_NUMBER "${_major} * 1000000 + ${_minor} * 10000 + ${_patch} * 100 + ${_step}")
message(STATUS "Carthage ${CARTHAGE_VERSION_STRING}, build ${CARTHAGE_BUILD_NUMBER}")
