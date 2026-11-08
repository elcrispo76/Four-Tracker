cmake_minimum_required(VERSION 3.22)

# macOS: one universal build for Apple Silicon and Intel Macs, macOS 11 or newer
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac CPUs")
endif()

project(FourTracker VERSION 0.5.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the app and plugins run without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Paths you can pass on the command line (-DNAME=path)
#   JUCE_DIR        : JUCE checkout (default: ./JUCE next to this file)
#   ASIO_SDK_DIR    : Steinberg ASIO SDK folder (the one containing "common")
#   VST2_SDK_DIR    : legacy VST2 SDK folder (the one containing "pluginterfaces")
# ---------------------------------------------------------------------------
set(JUCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")
set(ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (enables ASIO in the standalone app)")
set(VST2_SDK_DIR "" CACHE PATH "Path to the legacy VST2 SDK (enables the VST2 plugin)")

# Personal build with VST effect hosting (2 effects per channel, standalone app only).
option(FOURTRACKER_FX_HOST "Host VST effects in the standalone app" OFF)

# A build to give to others: the VST2 SDK is never used, even when it is in SDKs/
# (no VST2 plugin, no VST2 effect hosting). VST3 is MIT licensed and fine to share.
option(FOURTRACKER_NO_VST2 "Ignore the VST2 SDK (build to share)" OFF)

# Linux-only switch used for compile checks on machines without audio/X11 dev packages.
option(FOURTRACKER_HEADLESS_CHECK "Disable ALSA/X11 extensions (compile check only)" OFF)

add_subdirectory(${JUCE_DIR} JUCE)

# Windows: Standalone + VST3 (+ VST2 below). Mac: Standalone + VST3 + Audio Unit, never VST2.
if(APPLE)
    set(FT_FORMATS Standalone VST3 AU)
    set(FOURTRACKER_NO_VST2 ON)
    message(STATUS "  >>> MAC: Standalone + VST3 + Audio Unit (AU)")
else()
    set(FT_FORMATS Standalone VST3)
endif()

# ---- find the VST2 SDK -----------------------------------------------------
# Use VST2_SDK_DIR if given, otherwise search everything under SDKs/ for
# pluginterfaces/vst2.x/aeffect.h, so it works however the zip was unpacked.
if(VST2_SDK_DIR)
    file(TO_CMAKE_PATH "${VST2_SDK_DIR}" VST2_SDK_DIR)
endif()
if(FOURTRACKER_NO_VST2)
    message(STATUS "  >>> DELE-VERSION: VST2 SDK bliver IKKE brugt (kun VST3)")
endif()
if(NOT FOURTRACKER_NO_VST2 AND (NOT VST2_SDK_DIR OR NOT EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h"))
    file(GLOB_RECURSE _ft_aeffect LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/SDKs/*aeffect.h")
    foreach(_f IN LISTS _ft_aeffect)
        if(_f MATCHES "/pluginterfaces/vst2\\.x/aeffect\\.h$")
            get_filename_component(_d "${_f}" DIRECTORY)   # .../vst2.x
            get_filename_component(_d "${_d}" DIRECTORY)   # .../pluginterfaces
            get_filename_component(_d "${_d}" DIRECTORY)   # SDK root
            if(EXISTS "${_d}/pluginterfaces/vst2.x/aeffectx.h")
                set(VST2_SDK_DIR "${_d}")
                break()
            endif()
        endif()
    endforeach()
endif()

if(NOT FOURTRACKER_NO_VST2 AND VST2_SDK_DIR AND EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h")
    juce_set_vst2_sdk_path("${VST2_SDK_DIR}")
    list(APPEND FT_FORMATS VST)
    message(STATUS "")
    message(STATUS "  >>> VST2 SDK FUNDET: ${VST2_SDK_DIR}")
    message(STATUS "  >>> VST2 plugin (Four Tracker.dll) bliver bygget")
    message(STATUS "")
elseif(FOURTRACKER_NO_VST2)
    message(STATUS "  >>> Der bygges kun Standalone + VST3")
    message(STATUS "")
else()
    message(STATUS "")
    message(STATUS "  >>> VST2 SDK IKKE FUNDET under ${CMAKE_CURRENT_SOURCE_DIR}/SDKs")
    message(STATUS "  >>> Der bygges kun Standalone + VST3")
    message(STATUS "")
endif()

# ---- find the ASIO SDK -----------------------------------------------------
if(ASIO_SDK_DIR)
    file(TO_CMAKE_PATH "${ASIO_SDK_DIR}" ASIO_SDK_DIR)
endif()
if(WIN32 AND (NOT ASIO_SDK_DIR OR NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h"))
    file(GLOB_RECURSE _ft_asio LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/SDKs/*iasiodrv.h")
    foreach(_f IN LISTS _ft_asio)
        if(_f MATCHES "/common/iasiodrv\\.h$")
            get_filename_component(_d "${_f}" DIRECTORY)
            get_filename_component(_d "${_d}" DIRECTORY)
            set(ASIO_SDK_DIR "${_d}")
            break()
        endif()
    endforeach()
endif()

juce_add_plugin(FourTracker
    COMPANY_NAME                "FourTracker"
    PRODUCT_NAME                "Four Tracker"
    PLUGIN_MANUFACTURER_CODE    Ftrk
    PLUGIN_CODE                 Ft4c
    BUNDLE_ID                   com.fourtracker.fourtracker
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    FORMATS                     ${FT_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST2_CATEGORY               kPlugCategSynth
    VST3_CATEGORIES             Instrument Sampler
    MICROPHONE_PERMISSION_ENABLED FALSE
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_512.png"
    ICON_SMALL                  "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_512.png")

target_sources(FourTracker PRIVATE
    Source/TrackerTypes.cpp
    Source/Instrument.cpp
    Source/Synth2.cpp
    Source/AmigaSamples.cpp
    Source/TrackerEngine.cpp
    Source/DemoContent.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Widgets.cpp
    Source/UI/WaveformView.cpp
    Source/UI/PatternView.cpp
    Source/UI/HeaderBar.cpp
    Source/UI/Pages.cpp
    Source/UI/Mixer.cpp
    Source/UI/SettingsPage.cpp
    Source/UI/SampleBrowser.cpp
    Source/FxHost.cpp
    Source/JamRecorder.cpp
    Source/StandaloneApp.cpp
    Source/UI/FxPage.cpp)

target_include_directories(FourTracker PRIVATE Source)

target_compile_definitions(FourTracker PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

if(WIN32 AND ASIO_SDK_DIR)
    if(EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        target_compile_definitions(FourTracker PUBLIC JUCE_ASIO=1)
        target_include_directories(FourTracker PUBLIC "${ASIO_SDK_DIR}/common")
        message(STATUS "")
        message(STATUS "  >>> ASIO SDK FUNDET: ${ASIO_SDK_DIR}")
        message(STATUS "  >>> Standalone-programmet kan bruge ASIO")
        message(STATUS "")
    else()
        message(WARNING "ASIO_SDK_DIR does not contain common/iasiodrv.h - ASIO disabled")
    endif()
elseif(WIN32)
    message(STATUS "")
    message(STATUS "  >>> ASIO SDK IKKE FUNDET under ${CMAKE_CURRENT_SOURCE_DIR}/SDKs")
    message(STATUS "  >>> Programmet bygges uden ASIO (kun Windows Audio / DirectSound)")
    message(STATUS "")
endif()

if(FOURTRACKER_FX_HOST)
    if(APPLE)
        # Mac: Audio Unit and VST3 effects (/Library/Audio/Plug-Ins/Components and .../VST3), never VST2
        target_compile_definitions(FourTracker PUBLIC FOURTRACKER_FX_HOST=1 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1)
        message(STATUS "  >>> FX-VERSION (MAC): kan hente Audio Unit- og VST3-effekter")
    else()
    target_compile_definitions(FourTracker PUBLIC FOURTRACKER_FX_HOST=1 JUCE_PLUGINHOST_VST3=1)
    endif()
    if(APPLE)
    elseif(NOT FOURTRACKER_NO_VST2 AND VST2_SDK_DIR AND EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h")
        target_compile_definitions(FourTracker PUBLIC JUCE_PLUGINHOST_VST=1)
        target_include_directories(FourTracker PUBLIC "${VST2_SDK_DIR}")
        message(STATUS "  >>> FX-VERSION: kan hente VST3- og VST2-effekter")
    else()
        if(FOURTRACKER_NO_VST2)
            message(STATUS "  >>> FX-DELE-VERSION: kan hente VST3-effekter (ingen VST2) - maa gerne deles")
        else()
            message(STATUS "  >>> FX-VERSION: kan hente VST3-effekter (VST2 kraever VST2 SDK)")
        endif()
    endif()
endif()

if(FOURTRACKER_HEADLESS_CHECK)
    target_compile_definitions(FourTracker PUBLIC
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

# LAME 3.100 (LGPL, ThirdParty/lame): the MP3 encoder for REC, so a jam can be saved as MP3
file(GLOB FT_LAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lame/libmp3lame/*.c)
add_library(ft_lame STATIC ${FT_LAME_SOURCES})
target_include_directories(ft_lame
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lame ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lame/libmp3lame
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lame/include)
target_compile_definitions(ft_lame PRIVATE HAVE_CONFIG_H)
set_target_properties(ft_lame PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
if(MSVC)
    target_compile_options(ft_lame PRIVATE /W0)
else()
    target_compile_options(ft_lame PRIVATE -w)
endif()

# the program asks before closing with unsaved changes: our own standalone app (Source/StandaloneApp.cpp)
target_compile_definitions(FourTracker PUBLIC JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(FourTracker
    PRIVATE
        ft_lame
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)


# Optional offline test (engine, MIDI switching, state, screenshots).
option(FOURTRACKER_BUILD_TESTS "Build the offline test program" OFF)
if(FOURTRACKER_BUILD_TESTS)
    juce_add_console_app(FourTrackerTests PRODUCT_NAME "FourTrackerTests")
    get_target_property(FT_SOURCES FourTracker SOURCES)
    list(FILTER FT_SOURCES INCLUDE REGEX "^Source/")
    target_sources(FourTrackerTests PRIVATE Tests/EngineTest.cpp ${FT_SOURCES} PatternMaker/PatternGen.cpp PatternMaker/MakerComponent.cpp)
    target_include_directories(FourTrackerTests PRIVATE Source PatternMaker)
    target_compile_definitions(FourTrackerTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(FOURTRACKER_FX_HOST)
        if(APPLE)
            target_compile_definitions(FourTrackerTests PRIVATE FOURTRACKER_FX_HOST=1 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=0)
        else()
            target_compile_definitions(FourTrackerTests PRIVATE FOURTRACKER_FX_HOST=1 JUCE_PLUGINHOST_VST3=1)
        endif()
    endif()
    if(FOURTRACKER_HEADLESS_CHECK)
        target_compile_definitions(FourTrackerTests PRIVATE
            JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
    endif()
    target_link_libraries(FourTrackerTests PRIVATE ft_lame
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_devices juce::juce_audio_processors
        juce::juce_recommended_config_flags)
endif()


# Four Tracker Pattern Maker: a program of its own that makes beats, basslines and arps and saves
# them as .ftpat files for LOAD PATTERN. It plays them with Four Tracker's own engine.
option(FOURTRACKER_BUILD_PATTERN_MAKER "Build the Pattern Maker program" ON)
if(FOURTRACKER_BUILD_PATTERN_MAKER)
    juce_add_gui_app(FourTrackerPatternMaker
        PRODUCT_NAME "Four Tracker Pattern Maker"
        COMPANY_NAME "elcrispo76"
        BUNDLE_ID com.fourtracker.patternmaker
        VERSION 1.0.0
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_1024.png")
    get_target_property(FT_PM_SOURCES FourTracker SOURCES)
    list(FILTER FT_PM_SOURCES INCLUDE REGEX "^Source/")
    list(FILTER FT_PM_SOURCES EXCLUDE REGEX "StandaloneApp")
    target_sources(FourTrackerPatternMaker PRIVATE PatternMaker/Main.cpp PatternMaker/PatternGen.cpp PatternMaker/MakerComponent.cpp
        ${FT_PM_SOURCES})
    target_include_directories(FourTrackerPatternMaker PRIVATE Source PatternMaker)
    target_compile_definitions(FourTrackerPatternMaker PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_APPLICATION_NAME_STRING="Four Tracker Pattern Maker" JUCE_APPLICATION_VERSION_STRING="1.0.0")
    if(WIN32 AND ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        target_compile_definitions(FourTrackerPatternMaker PRIVATE JUCE_ASIO=1)
        target_include_directories(FourTrackerPatternMaker PRIVATE "${ASIO_SDK_DIR}/common")
    endif()
    if(FOURTRACKER_HEADLESS_CHECK)
        target_compile_definitions(FourTrackerPatternMaker PRIVATE
            JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
    endif()
    target_link_libraries(FourTrackerPatternMaker PRIVATE ft_lame
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_devices juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endif()
