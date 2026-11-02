cmake_minimum_required(VERSION 3.22)

# Dynamics Processor - transient shaper, compressor and saturation in one, with AUTO.
# Windows: VST3 64-bit and 32-bit (build-windows.bat).  Mac: VST3 + Audio Unit (build-mac.sh).

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac CPUs")
endif()

project(DynamicsProcessor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: a JUCE folder here, or one folder up (shared by several plugins)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE")
else()
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")
endif()
add_subdirectory(${JUCE_DIR} JUCE)

option(DYNPROC_TESTS "Build the tests" OFF)

if(APPLE)
    set(FX_FORMATS VST3 AU)
else()
    set(FX_FORMATS VST3)
endif()

# ---- VST2: only for your own use (build-windows-vst2.bat) -------------------
# The VST2 SDK may not be redistributed, and a VST2 build must never be shared.
# Put the SDK anywhere under SDKs/ (e.g. copy Four Tracker's SDKs folder here).
option(DYNPROC_VST2 "Also build a VST2 plugin (personal use only, needs the VST2 SDK in SDKs/)" OFF)
if(DYNPROC_VST2 AND NOT APPLE)
    set(_fx_vst2 "")
    file(GLOB_RECURSE _fx_aeffect LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/SDKs/*aeffect.h" "${CMAKE_CURRENT_SOURCE_DIR}/../SDKs/*aeffect.h")
    foreach(_f IN LISTS _fx_aeffect)
        if(_f MATCHES "/pluginterfaces/vst2\\.x/aeffect\\.h$")
            get_filename_component(_d "${_f}" DIRECTORY)
            get_filename_component(_d "${_d}" DIRECTORY)
            get_filename_component(_d "${_d}" DIRECTORY)
            if(EXISTS "${_d}/pluginterfaces/vst2.x/aeffectx.h")
                set(_fx_vst2 "${_d}")
                break()
            endif()
        endif()
    endforeach()
    if(_fx_vst2)
        juce_set_vst2_sdk_path("${_fx_vst2}")
        list(APPEND FX_FORMATS VST)
        message(STATUS "  >>> VST2 SDK FUNDET: ${_fx_vst2} - Dynamics Processor.dll bliver bygget (kun til dig selv)")
    else()
        message(FATAL_ERROR "  >>> VST2 SDK IKKE FUNDET under SDKs her eller en mappe op - kopier Four Trackers SDKs-mappe dertil")
    endif()
endif()

juce_add_plugin(DynamicsProcessor
    COMPANY_NAME                "FourTracker"
    PRODUCT_NAME                "Dynamics Processor"
    PLUGIN_MANUFACTURER_CODE    Ftrk
    PLUGIN_CODE                 FtDy
    BUNDLE_ID                   com.fourtracker.dynamicsprocessor
    AU_MAIN_TYPE                kAudioUnitType_Effect
    FORMATS                     ${FX_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx)

set(FX_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(FX_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

target_sources(DynamicsProcessor PRIVATE ${FX_SOURCES})
target_include_directories(DynamicsProcessor PRIVATE Source)
target_compile_definitions(DynamicsProcessor PUBLIC ${FX_DEFS})
target_link_libraries(DynamicsProcessor
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(DYNPROC_TESTS)
    juce_add_console_app(DynTests PRODUCT_NAME "DynTests")
    target_sources(DynTests PRIVATE Tests/DynTests.cpp ${FX_SOURCES})
    target_include_directories(DynTests PRIVATE Source)
    target_compile_definitions(DynTests PRIVATE ${FX_DEFS} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(DynTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
