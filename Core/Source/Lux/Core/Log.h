#pragma once

#include "Lux/Core/Base.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#pragma warning(push, 0)
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"
#pragma warning(pop)

namespace Lux {
	class Log
	{
	public:
		static void Init();

		static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;
	};
}

template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::vec<L, T, Q>& vector)
{
	return os << glm::to_string(vector);
}

template<typename OStream, glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::mat<C, R, T, Q>& matrix)
{
	return os << glm::to_string(matrix);
}

template<typename OStream, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, glm::qua<T, Q> quaternion)
{
	return os << glm::to_string(quaternion);
}

#define LUX_CORE_TRACE(...) ::Lux::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define LUX_CORE_INFO(...) ::Lux::Log::GetCoreLogger()->info(__VA_ARGS__)
#define LUX_CORE_WARN(...) ::Lux::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define LUX_CORE_ERROR(...) ::Lux::Log::GetCoreLogger()->error(__VA_ARGS__)
#define LUX_CORE_CRITICAL(...) ::Lux::Log::GetClientLogger()->critical(__VA_ARGS__)

#define LUX_TRACE(...) ::Lux::Log::GetClientLogger()->trace(__VA_ARGS__)
#define LUX_INFO(...) ::Lux::Log::GetClientLogger()->info(__VA_ARGS__)
#define LUX_WARN(...) ::Lux::Log::GetClientLogger()->warn(__VA_ARGS__)
#define LUX_ERROR(...) ::Lux::Log::GetClientLogger()->error(__VA_ARGS__)
#define LUX_CRITICAL(...) ::Lux::Log::GetClientLogger()->critical(__VA_ARGS__)
