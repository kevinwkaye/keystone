cmake_minimum_required(VERSION 3.22)

project(Keystone VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(KEYSTONE_WITH_NAM "Load NAM captures in the drive stage" ON)
option(KEYSTONE_BUILD_TESTS "Build the engine render tests and benchmark" ON)
option(KEYSTONE_BUILD_APP "Build the headless Keystone app (needs JUCE)" ON)
set(KEYSTONE_NAM_PATH "" CACHE PATH "Local NeuralAmpModelerCore checkout (optional)")
set(KEYSTONE_EIGEN_PATH "" CACHE PATH "Local Eigen include folder (optional)")
set(KEYSTONE_JUCE_PATH "" CACHE PATH "Local JUCE checkout (optional)")

include(FetchContent)

# ---------------------------------------------------------------------------
# NAM core (MIT) and Eigen (MPL2): portable, Windows x64 and Linux ARM64.
# ---------------------------------------------------------------------------
if(KEYSTONE_WITH_NAM)
    if(KEYSTONE_NAM_PATH)
        set(NAM_DIR ${KEYSTONE_NAM_PATH})
    else()
        FetchContent_Declare(nam_core
            GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
            GIT_TAG d3189ed482ded880c128bf312941266c4fa5ceec
            GIT_SUBMODULES "Dependencies/eigen"
            GIT_SHALLOW FALSE)
        FetchContent_GetProperties(nam_core)
        if(NOT nam_core_POPULATED)
            FetchContent_Populate(nam_core)
        endif()
        set(NAM_DIR ${nam_core_SOURCE_DIR})
    endif()
    if(KEYSTONE_EIGEN_PATH)
        set(EIGEN_DIR ${KEYSTONE_EIGEN_PATH})
    else()
        set(EIGEN_DIR ${NAM_DIR}/Dependencies/eigen)
    endif()
    file(GLOB NAM_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)
endif()

add_library(keystone_core STATIC
    core/src/Engine.cpp
    core/src/Params.cpp
    core/src/Analysis.cpp
    core/src/Controller.cpp)
target_include_directories(keystone_core PUBLIC core/include PRIVATE core/src)
if(KEYSTONE_WITH_NAM)
    # NAM registers its model types from static initialisers, which a static library
    # would drop at link time, so its objects are linked straight into each program.
    add_library(nam_objects OBJECT ${NAM_SOURCES})
    target_compile_definitions(nam_objects PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
    target_include_directories(nam_objects SYSTEM PUBLIC ${NAM_DIR} ${EIGEN_DIR} ${NAM_DIR}/Dependencies/nlohmann)
    if(MSVC)
        target_compile_definitions(nam_objects PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
        target_compile_options(nam_objects PRIVATE /O2 /fp:fast /bigobj /w)
    else()
        target_compile_options(nam_objects PRIVATE -O3 -w)
    endif()
    target_compile_definitions(keystone_core PUBLIC KEYSTONE_WITH_NAM NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
    target_include_directories(keystone_core SYSTEM PRIVATE ${NAM_DIR} ${EIGEN_DIR} ${NAM_DIR}/Dependencies/nlohmann)
    target_sources(keystone_core INTERFACE $<TARGET_OBJECTS:nam_objects>)
endif()
target_include_directories(keystone_core PUBLIC core/third_party)
if(MSVC)
    target_compile_definitions(keystone_core PUBLIC _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(keystone_core PRIVATE /O2 /fp:fast /bigobj /wd4244 /wd4267 /wd4305)
else()
    target_compile_options(keystone_core PRIVATE -O3 -ffast-math -fno-finite-math-only)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND KEYSTONE_WITH_NAM)
        target_link_libraries(keystone_core PUBLIC stdc++fs)
    endif()
endif()
find_package(Threads REQUIRED)
target_link_libraries(keystone_core PUBLIC Threads::Threads)

if(KEYSTONE_BUILD_TESTS)
    add_executable(keystone_tests core/tests/EngineTests.cpp)
    target_link_libraries(keystone_tests PRIVATE keystone_core)
    target_include_directories(keystone_tests PRIVATE core/src)
endif()

if(KEYSTONE_BUILD_APP)
    add_subdirectory(app)
endif()
