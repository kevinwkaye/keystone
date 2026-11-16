cmake_minimum_required(VERSION 3.22)

# Kuddle: Kevin's home rig (clean, warm bass) as a VST3 for Ableton, plus a standalone app.
# Its own project, so building it never touches the Keystone app. The engine is plain C++
# on Keystone's DSP helpers (../core/src), ready to move into the Keystone app and the Pi.
#   cmake -S kuddle -B build-kuddle && cmake --build build-kuddle --config Release

project(Kuddle VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(KUDDLE_BUILD_PLUGIN "Build the VST3 and standalone (needs JUCE)" ON)
option(KUDDLE_BUILD_TESTS "Build the engine tests and benchmark" ON)
set(KUDDLE_JUCE_PATH "" CACHE PATH "Local JUCE checkout (optional)")

set(KEYSTONE_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core/src)

if(KUDDLE_BUILD_TESTS)
    add_executable(kuddle_tests tests/KuddleTests.cpp)
    target_include_directories(kuddle_tests PRIVATE engine ${KEYSTONE_CORE})
    if(MSVC)
        target_compile_definitions(kuddle_tests PRIVATE _USE_MATH_DEFINES NOMINMAX)
        target_compile_options(kuddle_tests PRIVATE /O2 /fp:fast)
    else()
        target_compile_options(kuddle_tests PRIVATE -O3 -ffast-math -fno-finite-math-only)
    endif()
endif()

if(KUDDLE_BUILD_PLUGIN)
    if(KUDDLE_JUCE_PATH)
        add_subdirectory(${KUDDLE_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(Kuddle
        COMPANY_NAME "Kevin Kaye"
        BUNDLE_ID "uk.kevinkaye.kuddle"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Kudl
        FORMATS VST3 Standalone
        PRODUCT_NAME "Kuddle"
        DESCRIPTION "Clean, warm bass: mute, auto wah, valve DI, opto, tape, tone and room"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Distortion Dynamics EQ)

    juce_add_binary_data(KuddleData SOURCES ../app/Resources/RobotoCondensed-Bold.ttf)

    target_sources(Kuddle PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp
        plugin/PedalControl.cpp
        plugin/Keeper.cpp)

    target_include_directories(Kuddle PRIVATE plugin engine ${KEYSTONE_CORE})

    target_compile_definitions(Kuddle PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)
    if(MSVC)
        target_compile_definitions(Kuddle PRIVATE _USE_MATH_DEFINES NOMINMAX)
    endif()

    target_link_libraries(Kuddle
        PRIVATE
            KuddleData
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endif()
