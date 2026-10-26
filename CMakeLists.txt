cmake_minimum_required(VERSION 3.22)

# Breakbeat Processor - an 8 step breakbeat slicer effect.
# Windows: VST3 64-bit and 32-bit (build-windows.bat).  Mac: VST3 + Audio Unit (build-mac.sh).

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac CPUs")
endif()

project(BreakbeatProcessor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${JUCE_DIR} JUCE)

option(BREAKBEAT_TESTS "Build the tests" OFF)

if(APPLE)
    set(BB_FORMATS VST3 AU)
else()
    set(BB_FORMATS VST3)
endif()

# ---- VST2: only for your own use (build-windows-vst2.bat) -------------------
# The VST2 SDK may not be redistributed, and a VST2 build must never be shared.
# Put the SDK anywhere under SDKs/ (e.g. copy Four Tracker's SDKs folder here).
option(BREAKBEAT_VST2 "Also build a VST2 plugin (personal use only, needs the VST2 SDK in SDKs/)" OFF)
if(BREAKBEAT_VST2 AND NOT APPLE)
    set(_bb_vst2 "")
    file(GLOB_RECURSE _bb_aeffect LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/SDKs/*aeffect.h")
    foreach(_f IN LISTS _bb_aeffect)
        if(_f MATCHES "/pluginterfaces/vst2\\.x/aeffect\\.h$")
            get_filename_component(_d "${_f}" DIRECTORY)
            get_filename_component(_d "${_d}" DIRECTORY)
            get_filename_component(_d "${_d}" DIRECTORY)
            if(EXISTS "${_d}/pluginterfaces/vst2.x/aeffectx.h")
                set(_bb_vst2 "${_d}")
                break()
            endif()
        endif()
    endforeach()
    if(_bb_vst2)
        juce_set_vst2_sdk_path("${_bb_vst2}")
        list(APPEND BB_FORMATS VST)
        message(STATUS "  >>> VST2 SDK FUNDET: ${_bb_vst2} - Breakbeat Processor.dll bliver bygget (kun til dig selv)")
    else()
        message(FATAL_ERROR "  >>> VST2 SDK IKKE FUNDET under ${CMAKE_CURRENT_SOURCE_DIR}/SDKs - kopier Four Trackers SDKs-mappe hertil")
    endif()
endif()

juce_add_plugin(BreakbeatProcessor
    COMPANY_NAME                "FourTracker"
    PRODUCT_NAME                "Breakbeat Processor"
    PLUGIN_MANUFACTURER_CODE    Ftrk
    PLUGIN_CODE                 FtBb
    BUNDLE_ID                   com.fourtracker.breakbeatprocessor
    AU_MAIN_TYPE                kAudioUnitType_Effect
    FORMATS                     ${BB_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx)

set(BB_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(BB_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

target_sources(BreakbeatProcessor PRIVATE ${BB_SOURCES})
target_include_directories(BreakbeatProcessor PRIVATE Source)
target_compile_definitions(BreakbeatProcessor PUBLIC ${BB_DEFS})
target_link_libraries(BreakbeatProcessor
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(BREAKBEAT_TESTS)
    juce_add_console_app(BreakbeatTests PRODUCT_NAME "BreakbeatTests")
    target_sources(BreakbeatTests PRIVATE Tests/BreakbeatTests.cpp ${BB_SOURCES})
    target_include_directories(BreakbeatTests PRIVATE Source)
    target_compile_definitions(BreakbeatTests PRIVATE ${BB_DEFS} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(BreakbeatTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
