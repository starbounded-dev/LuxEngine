cmake_minimum_required(VERSION 3.20)
project(LuxAcousticsTests LANGUAGES C CXX)

# Renderer-free harness for the acoustics code. Engine occlusion, the material defaults and the stub
# backend build with no audio SDK. The Asterwave adapter test is added only when the private
# Asterwave source is present (Core/vendor/Asterwave, LUX_ASTERWAVE_ROOT or -DASTERWAVE_ROOT).
# The engine itself continues to use Premake.
get_filename_component(LUX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(LUX_SOURCE "${LUX_ROOT}/Core/Source/Lux")
if(NOT ASTERWAVE_ROOT)
    set(ASTERWAVE_ROOT "$ENV{LUX_ASTERWAVE_ROOT}")
endif()
if(NOT ASTERWAVE_ROOT)
    set(ASTERWAVE_ROOT "${LUX_ROOT}/Core/vendor/Asterwave")
endif()

find_package(Threads REQUIRED)

set(LUX_ACOUSTICS_COMMON
    "${LUX_SOURCE}/Audio/AcousticMaterial.cpp"
    "${LUX_SOURCE}/Core/UUID.cpp"
    "${LUX_SOURCE}/Core/Ref.cpp"
    "${LUX_SOURCE}/Serialization/Yaml.cpp"
    "${LUX_SOURCE}/Serialization/StreamWriter.cpp"
    "${LUX_SOURCE}/Serialization/StreamReader.cpp"
    "${LUX_ROOT}/Core/vendor/rapidyaml/ryml.cpp")

function(lux_acoustics_test name)
    add_executable(${name} ${ARGN} ${LUX_ACOUSTICS_COMMON})
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_include_directories(${name} PRIVATE
        "${LUX_ROOT}/Core/Source"
        "${LUX_ROOT}/Core/vendor/glm"
        "${LUX_ROOT}/Core/vendor/spdlog/include"
        "${LUX_ROOT}/Core/vendor/rapidyaml")
    target_compile_definitions(${name} PRIVATE
        SPDLOG_USE_STD_FORMAT NOMINMAX _CRT_SECURE_NO_WARNINGS LUX_DEBUG
        GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE)
    if(WIN32)
        target_compile_definitions(${name} PRIVATE LUX_PLATFORM_WINDOWS)
    else()
        target_compile_definitions(${name} PRIVATE LUX_PLATFORM_LINUX)
    endif()
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG /utf-8 /permissive- /Zc:preprocessor)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    target_link_libraries(${name} PRIVATE Threads::Threads)
endfunction()

enable_testing()

# The default build: no Asterwave, engine occlusion only.
lux_acoustics_test(lux_acoustics_stub_tests
    ../AcousticsStubTests.cpp
    "${LUX_SOURCE}/Audio/RaytracedAudioSceneStub.cpp")
add_test(NAME lux_acoustics_stub COMMAND lux_acoustics_stub_tests)
set_tests_properties(lux_acoustics_stub PROPERTIES TIMEOUT 45)

lux_acoustics_test(lux_occlusion_tests
    ../OcclusionTests.cpp
    "${LUX_SOURCE}/Audio/AudioOcclusion.cpp"
    "${LUX_SOURCE}/Audio/AudioOcclusionSettings.cpp"
    "${LUX_SOURCE}/Audio/AudioGeometrySystem.cpp"
    "${LUX_SOURCE}/Audio/RaytracedAudioSceneStub.cpp"
    "${LUX_SOURCE}/Serialization/FileStream.cpp")
add_test(NAME lux_engine_occlusion COMMAND lux_occlusion_tests "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lux_engine_occlusion PROPERTIES TIMEOUT 45)

# The opt-in Asterwave adapter, when its source is available.
if(EXISTS "${ASTERWAVE_ROOT}/CMakeLists.txt")
    set(ASTERWAVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASTERWAVE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${ASTERWAVE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/asterwave" EXCLUDE_FROM_ALL)
    lux_acoustics_test(lux_asterwave_tests
        ../RaytracedSceneTests.cpp
        "${LUX_SOURCE}/Audio/RaytracedAudioScene.cpp")
    target_link_libraries(lux_asterwave_tests PRIVATE asterwave_cpp)
    add_test(NAME lux_asterwave_adapter COMMAND lux_asterwave_tests)
    set_tests_properties(lux_asterwave_adapter PROPERTIES TIMEOUT 45)
else()
    message(STATUS "Asterwave source not found at ${ASTERWAVE_ROOT}; skipping the adapter test")
endif()
