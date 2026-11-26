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

juce_add_binary_data(KeystoneData SOURCES
    Resources/RobotoCondensed.ttf
    Resources/RobotoCondensed-Bold.ttf)

target_sources(Keystone PRIVATE
    src/Main.cpp
    src/AudioHost.cpp
    src/LinkServer.cpp
    src/PedalRack.cpp
    src/Projector.cpp
    ${PROJECT_SOURCE_DIR}/shared/PedalControl.cpp
    ${PROJECT_SOURCE_DIR}/shared/Keeper.cpp)

target_include_directories(Keystone PRIVATE src ${PROJECT_SOURCE_DIR}/shared ${PROJECT_SOURCE_DIR}/core/src)

# ASIO on Windows (Steinberg's ASIO SDK, used under its GPLv3 option), so interfaces whose
# Windows driver drops data in WASAPI can run through ASIO4ALL or their own ASIO driver.
if(WIN32)
    FetchContent_Declare(asiosdk
        GIT_REPOSITORY https://github.com/jcelerier/asio-2.git
        GIT_TAG 1bc070d1c7b8f0bf92f9d3b7c72e5880a7dcffdb)
    FetchContent_GetProperties(asiosdk)
    if(NOT asiosdk_POPULATED)
        FetchContent_Populate(asiosdk)
    endif()
    target_include_directories(Keystone PRIVATE ${asiosdk_SOURCE_DIR}/common)
endif()

target_compile_definitions(Keystone PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_ASIO=$<IF:$<BOOL:${WIN32}>,1,0>
    JUCE_ALSA=1
    JUCE_JACK=0
    JUCE_USE_WINRT_MIDI=0
    JUCE_APPLICATION_NAME_STRING="Keystone"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Keystone PRIVATE
    keystone_core
    KeystoneData
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_recommended_config_flags)

if(WIN32 OR KEYSTONE_FORCE_TRAY)
    # a tray icon for anyone with a screen; the Pi build stays headless
    target_compile_definitions(Keystone PRIVATE KEYSTONE_TRAY=1)
    target_link_libraries(Keystone PRIVATE juce::juce_gui_extra)
else()
    target_compile_definitions(Keystone PRIVATE KEYSTONE_TRAY=0)
endif()

# Pedal bench: every Kaleidoscope pedal through the rack, offline (CI runs it on each build)
juce_add_console_app(pedal_bench PRODUCT_NAME "pedal_bench")
target_sources(pedal_bench PRIVATE tests/PedalBench.cpp src/PedalRack.cpp)
target_include_directories(pedal_bench PRIVATE src ${PROJECT_SOURCE_DIR}/core/include)
target_compile_definitions(pedal_bench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LADSPA=0 JUCE_PLUGINHOST_LV2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(pedal_bench PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
