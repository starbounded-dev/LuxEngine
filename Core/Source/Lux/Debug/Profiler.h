#pragma once

// Off in Dist, and off entirely when the projects were generated with "--no-tracy"
// (which omits TRACY_ENABLE, reducing the vendored Tracy library to a stub).
#define LUX_ENABLE_PROFILING (!LUX_DIST && TRACY_ENABLE)

#if LUX_ENABLE_PROFILING
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#endif

#if LUX_ENABLE_PROFILING
	#define LUX_PROFILE_MARK_FRAME					FrameMark;
	#define LUX_PROFILE_FUNCTION(...)				ZoneScopedN(__VA_ARGS__)
	#define LUX_PROFILE_FUNC(...)					LUX_PROFILE_FUNCTION(__VA_ARGS__)
	#define LUX_PROFILE_FUNCTION_COLOR(name, ...)	ZoneScopedNC(name, __VA_ARGS__) // Color is in hexadecimal
	#define LUX_PROFILE_SCOPE(...)					LUX_PROFILE_FUNCTION(__VA_ARGS__)
	#define LUX_PROFILE_SCOPE_COLOR(name, ...)		LUX_PROFILE_FUNCTION_COLOR(name, __VA_ARGS__)
	#define LUX_PROFILE_SCOPE_DYNAMIC(NAME)			ZoneScoped; ZoneName(NAME, strlen(NAME))
	#define LUX_PROFILE_THREAD(...)					tracy::SetThreadName(__VA_ARGS__)
	// Auto-named zone: Tracy captures the enclosing function name/file/line from the
	// source location, so no name string is needed. Used for broad function-level coverage.
	#define LUX_PROFILE_FUNCTION_AUTO				ZoneScoped
		#else
#define LUX_PROFILE_MARK_FRAME
#define LUX_PROFILE_FUNCTION(...)
#define LUX_PROFILE_FUNC(...)
#define LUX_PROFILE_FUNCTION_COLOR(name, ...)
#define LUX_PROFILE_SCOPE(...)
#define LUX_PROFILE_SCOPE_COLOR(name, ...)
#define LUX_PROFILE_SCOPE_DYNAMIC(NAME)
#define LUX_PROFILE_THREAD(...)
#define LUX_PROFILE_FUNCTION_AUTO
#endif

