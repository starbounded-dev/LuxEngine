#pragma once

#include "Base.h"
#include "Log.h"

#ifdef LUX_PLATFORM_WINDOWS
#define LUX_DEBUG_BREAK __debugbreak()
#elif defined(LUX_COMPILER_CLANG)
#define LUX_DEBUG_BREAK __builtin_debugtrap()
#elif defined(LUX_COMPILER_GCC) && (defined(__i386__) || defined(__x86_64__))
#define LUX_DEBUG_BREAK __asm__ volatile("int $0x03")
#elif defined(LUX_COMPILER_GCC)
#define LUX_DEBUG_BREAK __builtin_trap()
#else
#define LUX_DEBUG_BREAK
#endif

#ifdef LUX_DEBUG
#define LUX_ENABLE_ASSERTS
#endif

#define LUX_ENABLE_VERIFY

#ifdef LUX_ENABLE_ASSERTS
#ifdef LUX_COMPILER_CLANG
#define LUX_CORE_ASSERT_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Core, "Assertion Failed", ##__VA_ARGS__)
#define LUX_ASSERT_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Client, "Assertion Failed", ##__VA_ARGS__)
#else
#define LUX_CORE_ASSERT_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Core, "Assertion Failed" __VA_OPT__(,) __VA_ARGS__)
#define LUX_ASSERT_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Client, "Assertion Failed" __VA_OPT__(,) __VA_ARGS__)
#endif

#define LUX_CORE_ASSERT(condition, ...) { if(!(condition)) { LUX_CORE_ASSERT_MESSAGE_INTERNAL(__VA_ARGS__); LUX_DEBUG_BREAK; } }
#define LUX_ASSERT(condition, ...) { if(!(condition)) { LUX_ASSERT_MESSAGE_INTERNAL(__VA_ARGS__); LUX_DEBUG_BREAK; } }
#else
#define LUX_CORE_ASSERT(condition, ...)
#define LUX_ASSERT(condition, ...)
#endif

#ifdef LUX_ENABLE_VERIFY
#ifdef LUX_COMPILER_CLANG
#define LUX_CORE_VERIFY_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Core, "Verify Failed", ##__VA_ARGS__)
#define LUX_VERIFY_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Client, "Verify Failed", ##__VA_ARGS__)
#else
#define LUX_CORE_VERIFY_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Core, "Verify Failed" __VA_OPT__(,) __VA_ARGS__)
#define LUX_VERIFY_MESSAGE_INTERNAL(...)  ::Lux::Log::PrintAssertMessage(::Lux::Log::Type::Client, "Verify Failed" __VA_OPT__(,) __VA_ARGS__)
#endif

#define LUX_CORE_VERIFY(condition, ...) { if(!(condition)) { LUX_CORE_VERIFY_MESSAGE_INTERNAL(__VA_ARGS__); LUX_DEBUG_BREAK; } }
#define LUX_VERIFY(condition, ...) { if(!(condition)) { LUX_VERIFY_MESSAGE_INTERNAL(__VA_ARGS__); LUX_DEBUG_BREAK; } }
#else
#define LUX_CORE_VERIFY(condition, ...)
#define LUX_VERIFY(condition, ...)
#endif
