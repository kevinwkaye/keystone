# Keystone headless app: Windows x64 now, Linux (Pi) later. JUCE handles WASAPI, ALSA,
# MIDI and sockets; the sound and the controller come from keystone_core.

if(KEYSTONE_JUCE_PATH)
    add_subdirectory(${KEYSTONE_JUCE_PATH} JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(Keystone
    PRODUCT_NAME "Keystone"
    COMPANY_NAME "Kevin Kaye"
    BUNDLE_ID "uk.kevinkaye.keystone"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)

target_sources(Keystone PRIVATE
    src/Main.cpp
    src/AudioHost.cpp
    src/LinkServer.cpp
    src/PedalControl.cpp
    src/Keeper.cpp)

target_include_directories(Keystone PRIVATE src ${PROJECT_SOURCE_DIR}/core/src)

target_compile_definitions(Keystone PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_ASIO=0
    JUCE_ALSA=1
    JUCE_JACK=0
    JUCE_USE_WINRT_MIDI=0
    JUCE_APPLICATION_NAME_STRING="Keystone"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Keystone PRIVATE
    keystone_core
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)

if(WIN32)
    # a tray icon for anyone with a screen; the Pi build stays headless
    target_compile_definitions(Keystone PRIVATE KEYSTONE_TRAY=1)
    target_link_libraries(Keystone PRIVATE juce::juce_gui_extra)
else()
    target_compile_definitions(Keystone PRIVATE KEYSTONE_TRAY=0)
endif()
