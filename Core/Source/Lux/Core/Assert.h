#pragma once

#include "StarEngine/Core/Base.h"
#include "StarEngine/Core/Log.h"
#include <filesystem>

#ifdef SE_ENABLE_ASSERTS

// Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
#define SE_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { SE##type##ERROR(msg, __VA_ARGS__); SE_DEBUGBREAK(); } }
#define SE_INTERNAL_ASSERT_WITH_MSG(type, check, ...) SE_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
#define SE_INTERNAL_ASSERT_NO_MSG(type, check) SE_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", SE_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

#define SE_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
#define SE_INTERNAL_ASSERT_GET_MACRO(...) SE_EXPAND_MACRO( SE_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, SE_INTERNAL_ASSERT_WITH_MSG, SE_INTERNAL_ASSERT_NO_MSG) )

// Currently accepts at least the condition and one additional parameter (the message) being optional
#define SE_ASSERT(...) SE_EXPAND_MACRO( SE_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
#define SE_CORE_ASSERT(...) SE_EXPAND_MACRO( SE_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
#define SE_ASSERT(...)
#define SE_CORE_ASSERT(...)
#endif

#ifdef SE_ENABLE_VERIFY

// Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
#define SE_INTERNAL_VERIFY_IMPL(type, check, msg, ...) { if(!(check)) { SE##type##ERROR(msg, __VA_ARGS__); SE_DEBUGBREAK(); } }
#define SE_INTERNAL_VERIFY_WITH_MSG(type, check, ...) SE_INTERNAL_VERIFY_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
#define SE_INTERNAL_VERIFY_NO_MSG(type, check) SE_INTERNAL_VERIFY_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", SE_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

#define SE_INTERNAL_VERIFY_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
#define SE_INTERNAL_VERIFY_GET_MACRO(...) SE_EXPAND_MACRO( SE_INTERNAL_VERIFY_GET_MACRO_NAME(__VA_ARGS__, SE_INTERNAL_VERIFY_WITH_MSG, SE_INTERNAL_VERIFY_NO_MSG) )

// Currently accepts at least the condition and one additional parameter (the message) being optional
#define SE_VERIFY(...) SE_EXPAND_MACRO( SE_INTERNAL_VERIFY_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
#define SE_CORE_VERIFY(...) SE_EXPAND_MACRO( SE_INTERNAL_VERIFY_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
#define SE_VERIFY(...)
#define SE_CORE_VERIFY(...)
#endif
