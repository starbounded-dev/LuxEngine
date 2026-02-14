#pragma once

#define LUX_VERSION "2026.1"

//
// Build Configuration
//
#if defined(LUX_DEBUG)
	#define LUX_BUILD_CONFIG_NAME "Debug"
#elif defined(LUX_RELEASE)
	#define LUX_BUILD_CONFIG_NAME "Release"
#elif defined(LUX_DIST)
	#define LUX_BUILD_CONFIG_NAME "Dist"
#else
	#error Undefined configuration?
#endif

//
// Build Platform
//
#if defined(LUX_PLATFORM_WINDOWS)
	#define LUX_BUILD_PLATFORM_NAME "Windows x64"
#elif defined(LUX_PLATFORM_LINUX)
	#define LUX_BUILD_PLATFORM_NAME "Linux"
#else
	#define LUX_BUILD_PLATFORM_NAME "Unknown"
#endif

#define LUX_VERSION_LONG "Lux " LUX_VERSION " (" LUX_BUILD_PLATFORM_NAME " " LUX_BUILD_CONFIG_NAME ")"
