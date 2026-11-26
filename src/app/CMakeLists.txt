# The Carthage application: app/ (windows, commands, paths, updates) and ui/ (components, theme).

# Updates (docs/RELEASING.md). The EdDSA public key is not secret; it lives in the repository so every
# build of a tag carries it. Empty until the owner generates the key pair: such builds do not update.
file(READ "${CMAKE_SOURCE_DIR}/packaging/update-public-key.txt" _key)
string(STRIP "${_key}" _key)
set(CARTHAGE_UPDATE_PUBLIC_KEY "${_key}" CACHE STRING "Sparkle/WinSparkle EdDSA public key (packaging/update-public-key.txt)" FORCE)
set(CARTHAGE_FEED_STABLE "https://github.com/JonneNieminen/Carthage/releases/latest/download/appcast.xml")
set(CARTHAGE_FEED_BETA "https://github.com/JonneNieminen/Carthage/releases/download/beta-feed/appcast-beta.xml")

set(_plist "<plist><dict>")
string(APPEND _plist "<key>SUFeedURL</key><string>${CARTHAGE_FEED_STABLE}</string>")
string(APPEND _plist "<key>SUEnableAutomaticChecks</key><false/>")
if(CARTHAGE_UPDATE_PUBLIC_KEY)
    string(APPEND _plist "<key>SUPublicEDKey</key><string>${CARTHAGE_UPDATE_PUBLIC_KEY}</string>")
endif()
string(APPEND _plist "</dict></plist>")

juce_add_gui_app(Carthage
    PRODUCT_NAME "Carthage"
    VERSION "${CARTHAGE_VERSION}"
    BUILD_VERSION "${CARTHAGE_BUILD_NUMBER}"
    BUNDLE_ID "io.github.jonnenieminen.carthage"
    COMPANY_NAME "Jonne Nieminen"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Carthage records and monitors the instruments plugged into your audio interface."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLIST_TO_MERGE "${_plist}")

target_sources(Carthage PRIVATE
    Main.cpp
    Commands.h
    Paths.cpp Paths.h
    Updater.cpp Updater.h
    platform/MicrophoneAccess.h
    $<IF:$<PLATFORM_ID:Darwin>,platform/MicrophoneAccess_mac.mm,platform/MicrophoneAccess_other.cpp>
)

if(APPLE)
    target_sources(Carthage PRIVATE platform/Updater_mac.mm)
    set_source_files_properties(platform/Updater_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
elseif(WIN32)
    target_sources(Carthage PRIVATE platform/Updater_win.cpp)
else()
    target_sources(Carthage PRIVATE platform/Updater_other.cpp)
endif()

target_compile_definitions(Carthage PRIVATE
    CARTHAGE_UPDATE_PUBLIC_KEY="${CARTHAGE_UPDATE_PUBLIC_KEY}"
    CARTHAGE_FEED_STABLE="${CARTHAGE_FEED_STABLE}"
    CARTHAGE_FEED_BETA="${CARTHAGE_FEED_BETA}")
target_link_libraries(Carthage PRIVATE carthage_updater_framework)

# The updater ships inside the app: Sparkle.framework in Contents/Frameworks (ditto keeps the
# framework's symlinks), WinSparkle.dll next to Carthage.exe.
if(APPLE)
    add_custom_command(TARGET Carthage POST_BUILD
        COMMAND rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:Carthage>/Frameworks/Sparkle.framework"
        COMMAND ditto "${CARTHAGE_SPARKLE_DIR}/Sparkle.framework" "$<TARGET_BUNDLE_CONTENT_DIR:Carthage>/Frameworks/Sparkle.framework"
        COMMENT "Embedding Sparkle.framework")
elseif(WIN32)
    add_custom_command(TARGET Carthage POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CARTHAGE_WINSPARKLE_DIR}/x64/Release/WinSparkle.dll" "$<TARGET_FILE_DIR:Carthage>/"
        COMMENT "Placing WinSparkle.dll next to the app")
endif()

target_include_directories(Carthage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Carthage PRIVATE carthage_ui carthage_warnings)

if(APPLE)
    target_link_libraries(Carthage PRIVATE "-framework AVFoundation")   # platform/MicrophoneAccess_mac.mm
endif()

# carthage-scan ships next to the app's executable (app::Paths::scanner()).
add_dependencies(Carthage carthage_scan)
add_custom_command(TARGET Carthage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:carthage_scan>
            $<TARGET_FILE_DIR:Carthage>/carthage-scan$<$<PLATFORM_ID:Windows>:.exe>
    COMMENT "Placing carthage-scan next to the app")

# carthage-plughost too: plugins in their own processes (5f, W65).
add_dependencies(Carthage carthage_plughost)
add_custom_command(TARGET Carthage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:carthage_plughost>
            $<TARGET_FILE_DIR:Carthage>/carthage-plughost$<$<PLATFORM_ID:Windows>:.exe>
    COMMENT "Placing carthage-plughost next to the app")

# The embedded typefaces' SIL Open Font License texts ship with the app (DECISIONS W45):
# Contents/Resources/Fonts on macOS, a Fonts folder next to Carthage.exe on Windows.
set(carthage_font_licences
    "${CMAKE_SOURCE_DIR}/resources/fonts/OFL-Barlow.txt"
    "${CMAKE_SOURCE_DIR}/resources/fonts/OFL-IBMPlexMono.txt")
if(APPLE)
    set(carthage_font_licence_dir "$<TARGET_BUNDLE_CONTENT_DIR:Carthage>/Resources/Fonts")
else()
    set(carthage_font_licence_dir "$<TARGET_FILE_DIR:Carthage>/Fonts")
endif()
add_custom_command(TARGET Carthage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${carthage_font_licence_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${carthage_font_licences} "${carthage_font_licence_dir}/"
    COMMENT "Placing the font licences with the app")
