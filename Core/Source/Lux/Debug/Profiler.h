#pragma once

#define LUX_ENABLE_PROFILING !LUX_DIST

#if LUX_ENABLE_PROFILING
#include <GLAD/glad.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>
#endif

#if LUX_ENABLE_PROFILING
	#define LUX_PROFILE_MARK_FRAME					FrameMark;
	#define LUX_PROFILE_FUNCTION(...)				ZoneScopedN(__VA_ARGS__)
	#define LUX_PROFILE_FUNCTION_COLOR(name, ...)	ZoneScopedNC(name, __VA_ARGS__) // Color is in hexadecimal
	#define LUX_PROFILE_SCOPE(...)					LUX_PROFILE_FUNCTION(__VA_ARGS__)
	#define LUX_PROFILE_SCOPE_COLOR(name, ...)		LUX_PROFILE_FUNCTION_COLOR(name, __VA_ARGS__)
	#define LUX_PROFILE_SCOPE_DYNAMIC(NAME)			ZoneScoped; ZoneName(NAME, strlen(NAME))
	#define LUX_PROFILE_THREAD(...)					tracy::SetThreadName(__VA_ARGS__)
	#define LUX_PROFILE_GPU_SCOPE(...)				TracyGpuZone(__VA_ARGS__)
#else
#define LUX_PROFILE_MARK_FRAME
#define LUX_PROFILE_FUNCTION(...)
#define LUX_PROFILE_FUNCTION_COLOR(name, ...)
#define LUX_PROFILE_SCOPE(...)
#define LUX_PROFILE_SCOPE_COLOR(name, ...)
#define LUX_PROFILE_SCOPE_DYNAMIC(NAME)
#define LUX_PROFILE_THREAD(...)
#define LUX_PROFILE_GPU_SCOPE(...)
#endif

