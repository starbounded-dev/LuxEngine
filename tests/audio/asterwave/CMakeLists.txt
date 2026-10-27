cmake_minimum_required(VERSION 3.20)
project(LuxAsterwaveTests LANGUAGES C CXX)

# A renderer-free harness compiling the actual Lux adapter and material serialization.
# The engine itself continues to use Premake; this target needs no FMOD/device/window.
get_filename_component(LUX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT ASTERWAVE_ROOT)
    set(ASTERWAVE_ROOT "$ENV{LUX_ASTERWAVE_ROOT}")
endif()
if(NOT ASTERWAVE_ROOT)
    set(ASTERWAVE_ROOT "${LUX_ROOT}/Core/vendor/Asterwave")
endif()
set(ASTERWAVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASTERWAVE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${ASTERWAVE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/asterwave" EXCLUDE_FROM_ALL)

add_executable(lux_asterwave_tests
    ../RaytracedSceneTests.cpp
    "${LUX_ROOT}/Core/Source/Lux/Audio/RaytracedAudioScene.cpp"
    "${LUX_ROOT}/Core/Source/Lux/Audio/AcousticMaterial.cpp"
    "${LUX_ROOT}/Core/Source/Lux/Core/UUID.cpp"
    "${LUX_ROOT}/Core/Source/Lux/Core/Ref.cpp"
    "${LUX_ROOT}/Core/Source/Lux/Serialization/Yaml.cpp"
    "${LUX_ROOT}/Core/Source/Lux/Serialization/StreamWriter.cpp"
    "${LUX_ROOT}/Core/Source/Lux/Serialization/StreamReader.cpp"
    "${LUX_ROOT}/Core/vendor/rapidyaml/ryml.cpp")
target_compile_features(lux_asterwave_tests PRIVATE cxx_std_20)
target_include_directories(lux_asterwave_tests PRIVATE
    "${LUX_ROOT}/Core/Source"
    "${LUX_ROOT}/Core/vendor/glm"
    "${LUX_ROOT}/Core/vendor/spdlog/include"
    "${LUX_ROOT}/Core/vendor/rapidyaml")
target_compile_definitions(lux_asterwave_tests PRIVATE
    SPDLOG_USE_STD_FORMAT NOMINMAX _CRT_SECURE_NO_WARNINGS LUX_DEBUG
    GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE)
if(WIN32)
    target_compile_definitions(lux_asterwave_tests PRIVATE LUX_PLATFORM_WINDOWS)
else()
    target_compile_definitions(lux_asterwave_tests PRIVATE LUX_PLATFORM_LINUX)
endif()
if(MSVC)
    target_compile_options(lux_asterwave_tests PRIVATE /UNDEBUG /utf-8 /permissive- /Zc:preprocessor)
else()
    target_compile_options(lux_asterwave_tests PRIVATE -UNDEBUG)
endif()
find_package(Threads REQUIRED)
target_link_libraries(lux_asterwave_tests PRIVATE asterwave_cpp Threads::Threads)
enable_testing()
add_test(NAME lux_asterwave_adapter COMMAND lux_asterwave_tests)
set_tests_properties(lux_asterwave_adapter PROPERTIES TIMEOUT 45)
