#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Log.h"
#include <filesystem>

#ifdef LUX_ENABLE_ASSERTS

// Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
#define LUX_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { SE##type##ERROR(msg, __VA_ARGS__); LUX_DEBUGBREAK(); } }
#define LUX_INTERNAL_ASSERT_WITH_MSG(type, check, ...) LUX_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
#define LUX_INTERNAL_ASSERT_NO_MSG(type, check) LUX_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", LUX_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

#define LUX_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
#define LUX_INTERNAL_ASSERT_GET_MACRO(...) LUX_EXPAND_MACRO( LUX_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, LUX_INTERNAL_ASSERT_WITH_MSG, LUX_INTERNAL_ASSERT_NO_MSG) )

// Currently accepts at least the condition and one additional parameter (the message) being optional
#define LUX_ASSERT(...) LUX_EXPAND_MACRO( LUX_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
#define LUX_CORE_ASSERT(...) LUX_EXPAND_MACRO( LUX_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
#define LUX_ASSERT(...)
#define LUX_CORE_ASSERT(...)
#endif

#ifdef LUX_ENABLE_VERIFY

// Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
#define LUX_INTERNAL_VERIFY_IMPL(type, check, msg, ...) { if(!(check)) { SE##type##ERROR(msg, __VA_ARGS__); LUX_DEBUGBREAK(); } }
#define LUX_INTERNAL_VERIFY_WITH_MSG(type, check, ...) LUX_INTERNAL_VERIFY_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
#define LUX_INTERNAL_VERIFY_NO_MSG(type, check) LUX_INTERNAL_VERIFY_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", LUX_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

#define LUX_INTERNAL_VERIFY_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
#define LUX_INTERNAL_VERIFY_GET_MACRO(...) LUX_EXPAND_MACRO( LUX_INTERNAL_VERIFY_GET_MACRO_NAME(__VA_ARGS__, LUX_INTERNAL_VERIFY_WITH_MSG, LUX_INTERNAL_VERIFY_NO_MSG) )

// Currently accepts at least the condition and one additional parameter (the message) being optional
#define LUX_VERIFY(...) LUX_EXPAND_MACRO( LUX_INTERNAL_VERIFY_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
#define LUX_CORE_VERIFY(...) LUX_EXPAND_MACRO( LUX_INTERNAL_VERIFY_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
#define LUX_VERIFY(...)
#define LUX_CORE_VERIFY(...)
#endif
