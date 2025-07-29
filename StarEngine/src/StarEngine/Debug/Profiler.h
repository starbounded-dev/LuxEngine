#pragma once

#define SE_ENABLE_PROFILING !SE_DIST

#if SE_ENABLE_PROFILING
#include <GLAD/glad.h>
#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>
#endif

#if SE_ENABLE_PROFILING
	#define SE_PROFILE_MARK_FRAME					FrameMark;
	#define SE_PROFILE_FUNCTION(...)				ZoneScopedN(__VA_ARGS__)
	#define SE_PROFILE_FUNCTION_COLOR(name, ...)	ZoneScopedNC(name, __VA_ARGS__) // Color is in hexadecimal
	#define SE_PROFILE_SCOPE(...)					SE_PROFILE_FUNCTION(__VA_ARGS__)
	#define SE_PROFILE_SCOPE_COLOR(name, ...)		SE_PROFILE_FUNCTION_COLOR(name, __VA_ARGS__)
	#define SE_PROFILE_SCOPE_DYNAMIC(NAME)			ZoneScoped; ZoneName(NAME, strlen(NAME))
	#define SE_PROFILE_THREAD(...)					tracy::SetThreadName(__VA_ARGS__)
	#define SE_PROFILE_GPU_SCOPE(...)				TracyGpuZone(__VA_ARGS__)
#else
#define SE_PROFILE_MARK_FRAME
#define SE_PROFILE_FUNCTION(...)
#define SE_PROFILE_FUNCTION_COLOR(name, ...)
#define SE_PROFILE_SCOPE(...)
#define SE_PROFILE_SCOPE_COLOR(name, ...)
#define SE_PROFILE_SCOPE_DYNAMIC(NAME)
#define SE_PROFILE_THREAD(...)
#define SE_PROFILE_GPU_SCOPE(...)
#endif

