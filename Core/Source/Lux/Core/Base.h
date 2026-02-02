#pragma once

#include "StarEngine/Core/PlatformDetection.h"

#include <memory>

#if defined(SE_PLATFORM_WINDOWS)
#define SE_DEBUGBREAK() __debugbreak()
#elif defined(SE_PLATFORM_LINUX)
#include <signal.h>
#define SE_DEBUGBREAK() raise(SIGTRAP)
#else
#error "Platform doesn't support debugbreak yet!"
#endif

#ifdef SE_DEBUG
#define SE_ENABLE_ASSERTS
#endif

#ifndef SE_DIST
#define SE_ENABLE_VERIFY
#endif

#define SE_EXPAND_MACRO(x) x
#define SE_STRINGIFY_MACRO(x) #x

#define BIT(x) (1 << x)

#define SE_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace StarEngine {

	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

}

#include "StarEngine/Core/Log.h"
#include "StarEngine/Core/Assert.h"
