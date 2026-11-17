# The in-app updaters (brief, "Updates and releases"; DECISIONS F10): Sparkle 2 on macOS and
# WinSparkle on Windows. Both are the projects' official release archives, pinned by version and
# SHA-256 (DECISIONS W25); a changed archive fails the download instead of building.
#
# Provides:
#   carthage_updater_framework   an INTERFACE target the app links (headers + library)
#   CARTHAGE_SPARKLE_DIR         the unpacked Sparkle release (Sparkle.framework, bin/ tools)
#   CARTHAGE_WINSPARKLE_DIR      the unpacked WinSparkle release (include/, x64/Release/)

include(FetchContent)

set(CARTHAGE_SPARKLE_VERSION "2.10.1")
set(CARTHAGE_SPARKLE_SHA256 "1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27")
set(CARTHAGE_WINSPARKLE_VERSION "0.9.4")
set(CARTHAGE_WINSPARKLE_SHA256 "6037df37fc263bd1650a1c4949681a9d40ffe991d01f35892a406cb5d103c976")

add_library(carthage_updater_framework INTERFACE)

if(APPLE)
    FetchContent_Declare(sparkle
        URL "https://github.com/sparkle-project/Sparkle/releases/download/${CARTHAGE_SPARKLE_VERSION}/Sparkle-${CARTHAGE_SPARKLE_VERSION}.tar.xz"
        URL_HASH "SHA256=${CARTHAGE_SPARKLE_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR "no-cmake-project")   # an archive of binaries: populate only
    FetchContent_MakeAvailable(sparkle)
    set(CARTHAGE_SPARKLE_DIR "${sparkle_SOURCE_DIR}" CACHE INTERNAL "Unpacked Sparkle release")

    target_compile_options(carthage_updater_framework INTERFACE "-F${sparkle_SOURCE_DIR}")
    target_link_options(carthage_updater_framework INTERFACE
        "-F${sparkle_SOURCE_DIR}" "-framework" "Sparkle" "-Wl,-rpath,@executable_path/../Frameworks")

elseif(WIN32)
    FetchContent_Declare(winsparkle
        URL "https://github.com/vslavik/winsparkle/releases/download/v${CARTHAGE_WINSPARKLE_VERSION}/WinSparkle-${CARTHAGE_WINSPARKLE_VERSION}.zip"
        URL_HASH "SHA256=${CARTHAGE_WINSPARKLE_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR "no-cmake-project")
    FetchContent_MakeAvailable(winsparkle)
    set(CARTHAGE_WINSPARKLE_DIR "${winsparkle_SOURCE_DIR}" CACHE INTERNAL "Unpacked WinSparkle release")

    target_include_directories(carthage_updater_framework SYSTEM INTERFACE "${winsparkle_SOURCE_DIR}/include")
    target_link_libraries(carthage_updater_framework INTERFACE "${winsparkle_SOURCE_DIR}/x64/Release/WinSparkle.lib")
endif()
