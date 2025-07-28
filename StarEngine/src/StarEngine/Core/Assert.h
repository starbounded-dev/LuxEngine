#pragma once

#include "Base.h"
#include "Log.h"

#ifdef SE_PLATFORM_WINDOWS
#define SE_DEBUG_BREAK __debugbreak()
#elif defined(SE_COMPILER_CLANG)
#define SE_DEBUG_BREAK __builtin_debugtrap()
#else
#define SE_DEBUG_BREAK
#endif

#ifdef SE_DEBUG
#define SE_ENABLE_ASSERTS
#endif

#define SE_ENABLE_VERIFY

#ifdef SE_ENABLE_ASSERTS
#ifdef SE_COMPILER_CLANG
#define SE_CORE_ASSERT_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Core, "Assertion Failed", ##__VA_ARGS__)
#define SE_ASSERT_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Client, "Assertion Failed", ##__VA_ARGS__)
#else
#define SE_CORE_ASSERT_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Core, "Assertion Failed" __VA_OPT__(,) __VA_ARGS__)
#define SE_ASSERT_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Client, "Assertion Failed" __VA_OPT__(,) __VA_ARGS__)
#endif

#define SE_CORE_ASSERT(condition, ...) { if(!(condition)) { SE_CORE_ASSERT_MESSAGE_INTERNAL(__VA_ARGS__); SE_DEBUG_BREAK; } }
#define SE_ASSERT(condition, ...) { if(!(condition)) { SE_ASSERT_MESSAGE_INTERNAL(__VA_ARGS__); SE_DEBUG_BREAK; } }
#else
#define SE_CORE_ASSERT(condition, ...)
#define SE_ASSERT(condition, ...)
#endif

#ifdef SE_ENABLE_VERIFY
#ifdef SE_COMPILER_CLANG
#define SE_CORE_VERIFY_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Core, "Verify Failed", ##__VA_ARGS__)
#define SE_VERIFY_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Client, "Verify Failed", ##__VA_ARGS__)
#else
#define SE_CORE_VERIFY_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Core, "Verify Failed" __VA_OPT__(,) __VA_ARGS__)
#define SE_VERIFY_MESSAGE_INTERNAL(...)  ::StarEngine::Log::PrintAssertMessage(::StarEngine::Log::Type::Client, "Verify Failed" __VA_OPT__(,) __VA_ARGS__)
#endif

#define SE_CORE_VERIFY(condition, ...) { if(!(condition)) { SE_CORE_VERIFY_MESSAGE_INTERNAL(__VA_ARGS__); SE_DEBUG_BREAK; } }
#define SE_VERIFY(condition, ...) { if(!(condition)) { SE_VERIFY_MESSAGE_INTERNAL(__VA_ARGS__); SE_DEBUG_BREAK; } }
#else
#define SE_CORE_VERIFY(condition, ...)
#define SE_VERIFY(condition, ...)
#endif
