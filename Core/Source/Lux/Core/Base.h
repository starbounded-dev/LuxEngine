#pragma once

#include "Lux/Core/PlatformDetection.h"

#include <memory>

#if defined(LUX_PLATFORM_WINDOWS)
#define LUX_DEBUGBREAK() __debugbreak()
#elif defined(LUX_PLATFORM_LINUX)
#include <signal.h>
#define LUX_DEBUGBREAK() raise(SIGTRAP)
#else
#error "Platform doesn't support debugbreak yet!"
#endif

#ifdef LUX_DEBUG
#define LUX_ENABLE_ASSERTS
#endif

#ifndef LUX_DIST
#define LUX_ENABLE_VERIFY
#endif

#define LUX_EXPAND_MACRO(x) x
#define LUX_STRINGIFY_MACRO(x) #x

#define BIT(x) (1 << x)

#define LUX_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Lux {

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

#include "Lux/Core/Log.h"
#include "Lux/Core/Assert.h"
