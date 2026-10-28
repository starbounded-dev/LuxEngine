cmake_minimum_required(VERSION 3.20)
project(LuxNativeAudioTests LANGUAGES C CXX)

# Phase 0 harness for the native audio backend (docs/AUDIO_NATIVE_BACKEND_PLAN.md). It builds the
# vendored miniaudio on its own; the engine still uses Premake and FMOD until Phase 2.
get_filename_component(LUX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(MINIAUDIO_ROOT "${LUX_ROOT}/Core/vendor/miniaudio")

find_package(Threads REQUIRED)

add_library(lux_miniaudio STATIC MiniaudioBuild.c)
target_include_directories(lux_miniaudio PUBLIC "${MINIAUDIO_ROOT}")
target_link_libraries(lux_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT WIN32)
    target_link_libraries(lux_miniaudio PUBLIC m)
endif()

function(lux_native_audio_executable name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_link_libraries(${name} PRIVATE lux_miniaudio)
    target_compile_definitions(${name} PRIVATE NOMINMAX
        LUX_NATIVE_AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        LUX_SAMPLE_MUSIC="${LUX_ROOT}/Editor/LuxSampleProject/Assets/Audio/SampleProject/Assets/FX/Music/Music_1.mp3")
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG /utf-8 /permissive-)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
endfunction()

# Offline checks; no audio device needed.
lux_native_audio_executable(lux_native_audio_tests NativeAudioTests.cpp)
# Stress benchmark; run by hand, prints the numbers recorded in the plan.
lux_native_audio_executable(lux_native_audio_bench NativeAudioBench.cpp)
# Plays a file through the default output device; run by hand on a machine with a sound card.
lux_native_audio_executable(lux_native_audio_smoke NativeAudioSmoke.cpp)

enable_testing()
add_test(NAME lux_native_audio COMMAND lux_native_audio_tests)
set_tests_properties(lux_native_audio PROPERTIES TIMEOUT 60)
