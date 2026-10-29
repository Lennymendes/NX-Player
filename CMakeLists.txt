cmake_minimum_required(VERSION 3.22)
project(NXPlayer VERSION 1.0.0 LANGUAGES C CXX)

# NX Player — lecteur audiophile bit-perfect (PCM / DXD / DSD) de la NX Audio Suite. Licence MIT.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

option(NX_ENABLE_STREAMING "Sources Qobuz / Tidal / Spotify / YouTube dans l'interface" OFF)
option(NX_LTO "Optimisation à l'édition de liens (versions publiées)" OFF)
option(NX_ENABLE_OPENGL "Arrière-plans GLSL des thèmes (juce_opengl)" ON)
set(NX_JUCE_DIR "" CACHE PATH "JUCE local (sinon téléchargé : 8.0.15)")
set(NX_ASIO_SDK_DIR "" CACHE PATH "Windows : dossier du SDK ASIO de Steinberg (active la sortie ASIO PCM + DSD natif)")

#-------------------------------------------------------------------------------- JUCE
if(NX_JUCE_DIR)
    add_subdirectory(${NX_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#-------------------------------------------------------------------------------- SQLite
# System SQLite on Linux / macOS when present, otherwise the amalgamation (Windows), built with FTS5.
option(NX_BUNDLED_SQLITE "Compiler SQLite depuis l'amalgamation (toujours sous Windows)" OFF)
if(NOT WIN32 AND NOT NX_BUNDLED_SQLITE)
    find_package(SQLite3 QUIET)
endif()
if(SQLite3_FOUND)
    set(NX_SQLITE_TARGET SQLite::SQLite3)
else()
    include(FetchContent)
    FetchContent_Declare(nx_sqlite_src
        GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
        GIT_TAG 15d0ff10ebc7e7225eced1de84bb52137000899b
        SOURCE_SUBDIR do-not-add-subdirectory)
    FetchContent_MakeAvailable(nx_sqlite_src)
    add_library(nx_sqlite3 STATIC ${nx_sqlite_src_SOURCE_DIR}/sqlite3.c)
    target_include_directories(nx_sqlite3 PUBLIC ${nx_sqlite_src_SOURCE_DIR})
    target_compile_definitions(nx_sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_ENABLE_FTS5 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
    if(NOT MSVC)
        target_compile_options(nx_sqlite3 PRIVATE -w)
        find_package(Threads REQUIRED)
        target_link_libraries(nx_sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    endif()
    set(NX_SQLITE_TARGET nx_sqlite3)
endif()

#-------------------------------------------------------------------------------- ALAC (Apple, Apache 2.0)
file(GLOB NX_ALAC_SOURCES CONFIGURE_DEPENDS ThirdParty/alac/*.c ThirdParty/alac/*.cpp)
add_library(nx_alac STATIC ${NX_ALAC_SOURCES})

# QR Code generator (Project Nayuki, MIT) — NX Remote
add_library(nx_qrcodegen STATIC ThirdParty/qrcodegen/qrcodegen.cpp)
target_include_directories(nx_qrcodegen PUBLIC ThirdParty/qrcodegen)
target_include_directories(nx_alac PUBLIC ThirdParty/alac)
if(MSVC)
    target_compile_options(nx_alac PRIVATE /W0)
else()
    target_compile_options(nx_alac PRIVATE -w)
endif()

#-------------------------------------------------------------------------------- Application
set(NX_NEEDS_CURL FALSE)
if(UNIX AND NOT APPLE)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        set(NX_NEEDS_CURL TRUE)
    endif()
endif()

juce_add_gui_app(NXPlayer
    PRODUCT_NAME "NX Player"
    COMPANY_NAME "NX Audio Suite"
    BUNDLE_ID "com.nxaudio.nxplayer"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon_1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon_256.png"
    MICROPHONE_PERMISSION_ENABLED FALSE
    DOCUMENT_EXTENSIONS flac wav aif aiff dsf dff m4a
    NEEDS_CURL ${NX_NEEDS_CURL}
    NEEDS_WEB_BROWSER FALSE
    PLIST_TO_MERGE [[<plist><dict>
        <key>NSLocalNetworkUsageDescription</key>
        <string>NX Player recherche et lit les serveurs de musique UPnP / DLNA (NAS, Jellyfin) de votre réseau local.</string>
        <key>NSBonjourServices</key>
        <array>
            <string>_http._tcp</string>
            <string>_upnp._tcp</string>
            <string>_jellyfin._tcp</string>
        </array>
    </dict></plist>]])

file(GLOB_RECURSE NX_SOURCES CONFIGURE_DEPENDS
    Source/*.cpp Source/*.h
    Tests/*.cpp Tests/*.h)
list(FILTER NX_SOURCES EXCLUDE REGEX ".*/win/.*")
list(FILTER NX_SOURCES EXCLUDE REGEX ".*_mac\\.(cpp|mm)$")
if(APPLE)
    list(APPEND NX_SOURCES Source/Output/CoreAudioExclusive_mac.cpp Source/Output/CoreAudioBackend_mac.cpp Source/Platform/MediaControls_mac.mm)
    set_source_files_properties(Source/Platform/MediaControls_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
if(WIN32)
    list(APPEND NX_SOURCES Source/Platform/win/MediaControls_win.cpp)
    list(APPEND NX_SOURCES Source/Output/win/WasapiExclusiveDevice.cpp Source/Output/win/WasapiExclusiveDevice.h)
    if(NX_ASIO_SDK_DIR)
        list(APPEND NX_SOURCES Source/Output/win/AsioDevice.cpp Source/Output/win/AsioDevice.h
             ${NX_ASIO_SDK_DIR}/common/asio.cpp
             ${NX_ASIO_SDK_DIR}/host/asiodrivers.cpp
             ${NX_ASIO_SDK_DIR}/host/pc/asiolist.cpp)
        target_include_directories(NXPlayer PRIVATE ${NX_ASIO_SDK_DIR}/common ${NX_ASIO_SDK_DIR}/host ${NX_ASIO_SDK_DIR}/host/pc)
        target_compile_definitions(NXPlayer PRIVATE NX_HAS_ASIO=1)
    endif()
endif()
target_sources(NXPlayer PRIVATE ${NX_SOURCES})
target_include_directories(NXPlayer PRIVATE Source)
target_compile_definitions(NXPlayer PRIVATE NX_ENABLE_STREAMING=$<IF:$<BOOL:${NX_ENABLE_STREAMING}>,1,0>)

# Built-in service credentials (optional). Given as -D options or environment variables (CI: repository
# secrets), compiled into the app so users only sign in with their own account. Empty = the source page asks.
foreach(_cred NX_SPOTIFY_CLIENT_ID NX_TIDAL_CLIENT_ID NX_TIDAL_CLIENT_SECRET NX_QOBUZ_APP_ID NX_QOBUZ_APP_SECRET)
    set(${_cred} "" CACHE STRING "Identifiant intégré (facultatif)")
    if("${${_cred}}" STREQUAL "" AND DEFINED ENV{${_cred}})
        set(${_cred} "$ENV{${_cred}}")
    endif()
    string(STRIP "${${_cred}}" _v)
    string(REGEX REPLACE "[^A-Za-z0-9_.:=+/-]" "" _v "${_v}")
    set(${_cred}_VALUE "${_v}")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/nx_builtin_credentials.h" CONTENT [[
// Generated by CMake: built-in service credentials (empty when not provided at build time).
#pragma once
#define NX_BUILTIN_SPOTIFY_CLIENT_ID   "@NX_SPOTIFY_CLIENT_ID_VALUE@"
#define NX_BUILTIN_TIDAL_CLIENT_ID     "@NX_TIDAL_CLIENT_ID_VALUE@"
#define NX_BUILTIN_TIDAL_CLIENT_SECRET "@NX_TIDAL_CLIENT_SECRET_VALUE@"
#define NX_BUILTIN_QOBUZ_APP_ID        "@NX_QOBUZ_APP_ID_VALUE@"
#define NX_BUILTIN_QOBUZ_APP_SECRET    "@NX_QOBUZ_APP_SECRET_VALUE@"
]] @ONLY)
target_include_directories(NXPlayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Bundled themes and plug-ins (copied next to the executable / into the bundle Resources).
juce_add_binary_data(NXPlayerData
    HEADER_NAME NXPlayerData.h
    NAMESPACE NXData
    SOURCES Resources/AppIcon_256.png Resources/i18n/en.json Resources/remote/index.html)

set(NX_GL_MODULE "")
if(NX_ENABLE_OPENGL)
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND OR APPLE OR WIN32)
        set(NX_GL_MODULE juce::juce_opengl)
        target_compile_definitions(NXPlayer PRIVATE NX_HAS_OPENGL=1)
    endif()
endif()

find_package(ALSA QUIET)
set(NX_ALSA 0)
if(ALSA_FOUND)
    set(NX_ALSA 1)
endif()

target_compile_definitions(NXPlayer PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NXPlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NXPlayer,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_FLAC=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_ASIO=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_JACK=0
    JUCE_ALSA=${NX_ALSA}
    JUCE_WEB_BROWSER=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1
    NX_VERSION="${PROJECT_VERSION}")

if(UNIX AND NOT APPLE)
    # Dev headers absent from minimal Linux boxes are optional for JUCE (loaded at run time).
    include(CheckIncludeFile)
    check_include_file("X11/extensions/Xrandr.h" NX_HAVE_XRANDR)
    check_include_file("X11/extensions/Xinerama.h" NX_HAVE_XINERAMA)
    check_include_file("X11/Xcursor/Xcursor.h" NX_HAVE_XCURSOR)
    check_include_file("X11/extensions/Xcomposite.h" NX_HAVE_XCOMPOSITE)
    target_compile_definitions(NXPlayer PRIVATE
        JUCE_USE_XRANDR=$<BOOL:${NX_HAVE_XRANDR}>
        JUCE_USE_XINERAMA=$<BOOL:${NX_HAVE_XINERAMA}>
        JUCE_USE_XCURSOR=$<BOOL:${NX_HAVE_XCURSOR}>
        JUCE_USE_XRENDER=$<BOOL:${NX_HAVE_XCOMPOSITE}>
        JUCE_USE_CURL=$<BOOL:${CURL_FOUND}>)
endif()

target_link_libraries(NXPlayer PRIVATE
    NXPlayerData
    nx_alac
    nx_qrcodegen
    ${NX_SQLITE_TARGET}
    juce::juce_core
    juce::juce_events
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_cryptography
    juce::juce_javascript
    ${NX_GL_MODULE}
    PUBLIC
    juce::juce_recommended_config_flags
    $<$<BOOL:${NX_LTO}>:juce::juce_recommended_lto_flags>)

if(MSVC)
    target_compile_options(NXPlayer PRIVATE /W4 /wd4100 /wd4244 /wd4267 /wd4305 /wd4458 /utf-8)
else()
    target_compile_options(NXPlayer PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-multichar)
endif()

if(WIN32)
    target_link_libraries(NXPlayer PRIVATE avrt ole32 ksuser windowsapp psapi)
endif()
if(APPLE)
    target_link_libraries(NXPlayer PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework MediaPlayer" "-framework IOKit" "-framework CoreFoundation")
endif()

# Thèmes et plug-ins livrés avec l'application (Catalog : plug-ins supplémentaires à installer en un clic).
set(NX_RUNTIME_DIRS Themes Plugins Catalog)
foreach(dir IN LISTS NX_RUNTIME_DIRS)
    if(APPLE)
        add_custom_command(TARGET NXPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/${dir}
                    "$<TARGET_BUNDLE_CONTENT_DIR:NXPlayer>/Resources/${dir}")
    else()
        add_custom_command(TARGET NXPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/${dir}
                    "$<TARGET_FILE_DIR:NXPlayer>/${dir}")
    endif()
endforeach()

#-------------------------------------------------------------------------------- Exemple de plug-in DSP (C pur, ABI stable)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/DSP/crossfeed/crossfeed.c)
    add_library(nx_crossfeed MODULE Plugins/DSP/crossfeed/crossfeed.c)
    target_include_directories(nx_crossfeed PRIVATE Source/Ext)
    set_target_properties(nx_crossfeed PROPERTIES PREFIX "" OUTPUT_NAME "crossfeed")
    if(NOT WIN32)
        target_link_libraries(nx_crossfeed PRIVATE m)
    endif()
    add_dependencies(NXPlayer nx_crossfeed)
    if(APPLE)
        set(NX_PLUGIN_DEST "$<TARGET_BUNDLE_CONTENT_DIR:NXPlayer>/Resources/Plugins/DSP/crossfeed")
    else()
        set(NX_PLUGIN_DEST "$<TARGET_FILE_DIR:NXPlayer>/Plugins/DSP/crossfeed")
    endif()
    add_custom_command(TARGET NXPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NX_PLUGIN_DEST}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:nx_crossfeed> "${NX_PLUGIN_DEST}/")
endif()

#-------------------------------------------------------------------------------- Catalogue : plug-in DSP « Tonalité & balance »
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Catalog/DSP/tone/tone.c)
    add_library(nx_tone MODULE Catalog/DSP/tone/tone.c)
    target_include_directories(nx_tone PRIVATE Source/Ext)
    set_target_properties(nx_tone PROPERTIES PREFIX "" OUTPUT_NAME "tone")
    if(NOT WIN32)
        target_link_libraries(nx_tone PRIVATE m)
    endif()
    add_dependencies(NXPlayer nx_tone)
    if(APPLE)
        set(NX_TONE_DEST "$<TARGET_BUNDLE_CONTENT_DIR:NXPlayer>/Resources/Catalog/DSP/tone")
    else()
        set(NX_TONE_DEST "$<TARGET_FILE_DIR:NXPlayer>/Catalog/DSP/tone")
    endif()
    add_custom_command(TARGET NXPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NX_TONE_DEST}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:nx_tone> "${NX_TONE_DEST}/")
endif()

#-------------------------------------------------------------------------------- macOS : signature ad hoc stable
# Une signature cohérente (identifiant fixe, bundle scellé) permet à macOS de mémoriser l'autorisation
# « Réseau local » au lieu de la redemander à chaque lancement. Doit rester la dernière étape POST_BUILD
# (après la copie des thèmes, plug-ins et du catalogue dans le bundle).
if(APPLE)
    add_custom_command(TARGET NXPlayer POST_BUILD
        COMMAND codesign --force --deep --sign - --identifier "com.nxaudio.nxplayer" "$<TARGET_BUNDLE_DIR:NXPlayer>"
        COMMENT "Signature ad hoc de NX Player.app (com.nxaudio.nxplayer)")
endif()
