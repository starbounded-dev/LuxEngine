cmake_minimum_required(VERSION 3.20)
project(LuxNativeAudioTests LANGUAGES C CXX)

# Harness for the native audio backend (docs/AUDIO_NATIVE_BACKEND_PLAN.md): the Phase 0 checks of the
# vendored miniaudio on its own, and the engine's backend (Core/Source/Lux/Audio/Native) driven
# through AudioEngine and AudioEventInstance. The engine itself builds with Premake.
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

# The engine's native backend. It compiles the backend's own miniaudio unit rather than linking
# lux_miniaudio: the engine raises MA_ENGINE_MAX_LISTENERS (Audio/Native/Miniaudio.h), which changes
# miniaudio's struct sizes, so the two builds of miniaudio must never meet in one program.
set(LUX_SOURCE "${LUX_ROOT}/Core/Source/Lux")
add_library(lux_engine_miniaudio STATIC "${LUX_SOURCE}/Audio/Native/MiniaudioImplementation.c")
target_include_directories(lux_engine_miniaudio PRIVATE "${MINIAUDIO_ROOT}")
target_link_libraries(lux_engine_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT WIN32)
    target_link_libraries(lux_engine_miniaudio PUBLIC m)
endif()

add_executable(lux_native_backend_tests NativeBackendTests.cpp
    "${LUX_SOURCE}/Audio/Native/NativeAudioEngine.cpp"
    "${LUX_SOURCE}/Audio/Native/NativeAudioEventInstance.cpp"
    "${LUX_SOURCE}/Audio/Native/NativeAudioBackend.cpp"
    "${LUX_SOURCE}/Audio/Native/NativeAudioPerformance.cpp"
    "${LUX_SOURCE}/Audio/Native/NativeAudioFileUtils.cpp"
    "${LUX_SOURCE}/Audio/AudioListener.cpp"
    "${LUX_SOURCE}/Audio/AudioPerformanceSettings.cpp"
    "${LUX_SOURCE}/Audio/AudioBankManifest.cpp"
    "${LUX_SOURCE}/Audio/AudioEventRefSerializer.cpp"
    "${LUX_SOURCE}/Audio/Sound.cpp"
    "${LUX_SOURCE}/Audio/PhysicsAudioSystem.cpp"
    "${LUX_SOURCE}/Audio/AudioSurfaceTable.cpp"
    "${LUX_SOURCE}/Audio/AcousticMaterial.cpp"
    "${LUX_ROOT}/Core/vendor/soundwave/src/AcousticMaterialSettings.cpp" # the default material table
    "${LUX_SOURCE}/Core/UUID.cpp"
    "${LUX_SOURCE}/Core/Ref.cpp"
    "${LUX_SOURCE}/Utilities/StringUtils.cpp"
    $<IF:$<BOOL:${WIN32}>,${LUX_ROOT}/Core/Platform/Windows/WindowsThread.cpp,${LUX_ROOT}/Core/Platform/Linux/LinuxThread.cpp>
    "${LUX_SOURCE}/Serialization/Yaml.cpp"
    "${LUX_SOURCE}/Serialization/StreamWriter.cpp"
    "${LUX_SOURCE}/Serialization/StreamReader.cpp"
    "${LUX_ROOT}/Core/vendor/rapidyaml/ryml.cpp")
target_compile_features(lux_native_backend_tests PRIVATE cxx_std_20)
target_include_directories(lux_native_backend_tests PRIVATE
    "${LUX_ROOT}/Core/Source"
    "${LUX_ROOT}/Core/vendor/glm"
    "${LUX_ROOT}/Core/vendor/spdlog/include"
    "${LUX_ROOT}/Core/vendor/rapidyaml"
    "${LUX_ROOT}/Core/vendor" # choc, as StringUtils.cpp includes it
    "${MINIAUDIO_ROOT}"
    "${LUX_ROOT}/Core/vendor/soundwave/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_definitions(lux_native_backend_tests PRIVATE
    SPDLOG_USE_STD_FORMAT NOMINMAX _CRT_SECURE_NO_WARNINGS LUX_DEBUG
    GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE
    LUX_NATIVE_AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(WIN32)
    # Premake builds Core with the Unicode character set; WindowsThread.cpp relies on it.
    target_compile_definitions(lux_native_backend_tests PRIVATE LUX_PLATFORM_WINDOWS UNICODE _UNICODE)
else()
    target_compile_definitions(lux_native_backend_tests PRIVATE LUX_PLATFORM_LINUX)
endif()
if(MSVC)
    target_compile_options(lux_native_backend_tests PRIVATE /UNDEBUG /utf-8 /permissive- /Zc:preprocessor)
else()
    # As premake5.lua does for the engine: GCC otherwise rejects Lux headers (Asset.h's Asset members).
    target_compile_options(lux_native_backend_tests PRIVATE -UNDEBUG $<$<CXX_COMPILER_ID:GNU>:-Wno-changes-meaning>)
endif()
target_link_libraries(lux_native_backend_tests PRIVATE lux_engine_miniaudio Threads::Threads)
add_test(NAME lux_native_backend COMMAND lux_native_backend_tests "${CMAKE_CURRENT_BINARY_DIR}/backend-fixtures")
set_tests_properties(lux_native_backend PROPERTIES TIMEOUT 120)
