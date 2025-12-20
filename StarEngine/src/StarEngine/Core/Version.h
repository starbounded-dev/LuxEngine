#pragma once

#define SE_VERSION "2025.1"

//
// Build Configuration
//
#if defined(SE_DEBUG)
#define SE_BUILD_CONFIG_NAME "Debug"
#elif defined(SE_RELEASE)
#define SE_BUILD_CONFIG_NAME "Release"
#elif defined(SE_DIST)
#define SE_BUILD_CONFIG_NAME "Dist"
#else
#error Undefined configuration?
#endif

//
// Build Platform
//
#if defined(SE_PLATFORM_WINDOWS)
#define SE_BUILD_PLATFORM_NAME "Windows x64"
#elif defined(SE_PLATFORM_LINUX)
#define SE_BUILD_PLATFORM_NAME "Linux"
#else
#define SE_BUILD_PLATFORM_NAME "Unknown"
#endif

#define SE_VERSION_LONG "StarEngine " SE_VERSION " (" SE_BUILD_PLATFORM_NAME " " SE_BUILD_CONFIG_NAME ")"
