// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// The release series: 2026.N is the year's Nth. CI builds append the commit ("2026.1-a3f9c1d"):
// the workflow writes Lux/Core/BuildVersion.h (gitignored), which defines LUX_VERSION first.
#define LUX_VERSION_SERIES "2026.1"

#if __has_include("Lux/Core/BuildVersion.h")
	#include "Lux/Core/BuildVersion.h"
#endif
#ifndef LUX_VERSION
	#define LUX_VERSION LUX_VERSION_SERIES
#endif

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
