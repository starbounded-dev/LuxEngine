#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/LogCustomFormatters.h"

#include <spdlog/spdlog.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// LUX_DIST may not be defined in Debug/Release builds
#ifndef LUX_DIST
	#define LUX_DIST 0
#endif

#define LUX_ASSERT_MESSAGE_BOX (!LUX_DIST && LUX_PLATFORM_WINDOWS && false)

#if LUX_ASSERT_MESSAGE_BOX
	#ifdef LUX_PLATFORM_WINDOWS
		#include <Windows.h>
	#endif
#endif

namespace Lux {

	class Log
	{
	public:
		enum class Type : uint8_t
		{
			Core = 0, Client = 1
		};
		enum class Level : uint8_t
		{
			Trace = 0, Info, Warn, Error, Fatal
		};
		struct TagDetails
		{
			bool Enabled = true;
			Level LevelFilter = Level::Trace;
		};

	public:
		static void Init();
		static void Shutdown();

		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
		inline static std::shared_ptr<spdlog::logger>& GetEditorConsoleLogger() { return s_EditorConsoleLogger; }

		static bool HasTag(const std::string& tag) { return s_EnabledTags.find(tag) != s_EnabledTags.end(); }
		static std::map<std::string, TagDetails>& EnabledTags() { return s_EnabledTags; }
		static void SetDefaultTagSettings();

#if defined(LUX_PLATFORM_WINDOWS)
		template<typename... Args>
		static void PrintMessage(Log::Type type, Log::Level level, std::format_string<Args...> format, Args&&... args);
#else
		template<typename... Args>
		static void PrintMessage(Log::Type type, Log::Level level, const std::string_view format, Args&&... args);
#endif

		template<typename... Args>
		static void PrintMessageTag(Log::Type type, Log::Level level, std::string_view tag, std::format_string<Args...> format, Args&&... args);

		static void PrintMessageTag(Log::Type type, Log::Level level, std::string_view tag, std::string_view message);

		template<typename... Args>
		static void PrintAssertMessage(Log::Type type, std::string_view prefix, std::format_string<Args...> message, Args&&... args);

		static void PrintAssertMessage(Log::Type type, std::string_view prefix);

	public:
		// Enum utils
		static const char* LevelToString(Level level)
		{
			switch (level)
			{
				case Level::Trace: return "Trace";
				case Level::Info:  return "Info";
				case Level::Warn:  return "Warn";
				case Level::Error: return "Error";
				case Level::Fatal: return "Fatal";
			}
			return "";
		}
		static Level LevelFromString(std::string_view string)
		{
			if (string == "Trace") return Level::Trace;
			if (string == "Info")  return Level::Info;
			if (string == "Warn")  return Level::Warn;
			if (string == "Error") return Level::Error;
			if (string == "Fatal") return Level::Fatal;

			return Level::Trace;
		}

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
		static std::shared_ptr<spdlog::logger> s_EditorConsoleLogger;

		inline static std::map<std::string, TagDetails> s_EnabledTags;
		static std::map<std::string, TagDetails> s_DefaultTagDetails;
	};

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tagged logs (prefer these!)                                                                                      //
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Core logging
#define LUX_CORE_TRACE_TAG(tag, ...) ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Core, ::Lux::Log::Level::Trace, tag, __VA_ARGS__)
#define LUX_CORE_INFO_TAG(tag, ...)  ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Core, ::Lux::Log::Level::Info, tag, __VA_ARGS__)
#define LUX_CORE_WARN_TAG(tag, ...)  ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Core, ::Lux::Log::Level::Warn, tag, __VA_ARGS__)
#define LUX_CORE_ERROR_TAG(tag, ...) ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Core, ::Lux::Log::Level::Error, tag, __VA_ARGS__)
#define LUX_CORE_FATAL_TAG(tag, ...) ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Core, ::Lux::Log::Level::Fatal, tag, __VA_ARGS__)

// Client logging
#define LUX_TRACE_TAG(tag, ...) ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Client, ::Lux::Log::Level::Trace, tag, __VA_ARGS__)
#define LUX_INFO_TAG(tag, ...)  ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Client, ::Lux::Log::Level::Info, tag, __VA_ARGS__)
#define LUX_WARN_TAG(tag, ...)  ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Client, ::Lux::Log::Level::Warn, tag, __VA_ARGS__)
#define LUX_ERROR_TAG(tag, ...) ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Client, ::Lux::Log::Level::Error, tag, __VA_ARGS__)
#define LUX_FATAL_TAG(tag, ...) ::Lux::Log::PrintMessageTag(::Lux::Log::Type::Client, ::Lux::Log::Level::Fatal, tag, __VA_ARGS__)

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Core Logging
#define LUX_CORE_TRACE(...)    ::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Trace, __VA_ARGS__)
#define LUX_CORE_INFO(...)     ::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Info, __VA_ARGS__)
#define LUX_CORE_WARN(...)     ::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Warn, __VA_ARGS__)
#define LUX_CORE_ERROR(...)    ::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Error, __VA_ARGS__)
#define LUX_CORE_FATAL(...)    ::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Fatal, __VA_ARGS__)
#define LUX_CORE_CRITICAL(...) ::Lux::Log::PrintMessage(::Lux::Log::Type::Core, ::Lux::Log::Level::Fatal, __VA_ARGS__)

// Client Logging
#define LUX_TRACE(...)   ::Lux::Log::PrintMessage(::Lux::Log::Type::Client, ::Lux::Log::Level::Trace, __VA_ARGS__)
#define LUX_INFO(...)    ::Lux::Log::PrintMessage(::Lux::Log::Type::Client, ::Lux::Log::Level::Info, __VA_ARGS__)
#define LUX_WARN(...)    ::Lux::Log::PrintMessage(::Lux::Log::Type::Client, ::Lux::Log::Level::Warn, __VA_ARGS__)
#define LUX_ERROR(...)   ::Lux::Log::PrintMessage(::Lux::Log::Type::Client, ::Lux::Log::Level::Error, __VA_ARGS__)
#define LUX_FATAL(...)   ::Lux::Log::PrintMessage(::Lux::Log::Type::Client, ::Lux::Log::Level::Fatal, __VA_ARGS__)

// Editor Console Logging Macros
#define LUX_CONSOLE_LOG_TRACE(...)   Lux::Log::GetEditorConsoleLogger()->trace(__VA_ARGS__)
#define LUX_CONSOLE_LOG_INFO(...)    Lux::Log::GetEditorConsoleLogger()->info(__VA_ARGS__)
#define LUX_CONSOLE_LOG_WARN(...)    Lux::Log::GetEditorConsoleLogger()->warn(__VA_ARGS__)
#define LUX_CONSOLE_LOG_ERROR(...)   Lux::Log::GetEditorConsoleLogger()->error(__VA_ARGS__)
#define LUX_CONSOLE_LOG_FATAL(...)   Lux::Log::GetEditorConsoleLogger()->critical(__VA_ARGS__)

namespace Lux {

#if defined(LUX_PLATFORM_WINDOWS)
	template<typename... Args>
	void Log::PrintMessage(Log::Type type, Log::Level level, std::format_string<Args...> format, Args&&... args)
#else
	template<typename... Args>
	void Log::PrintMessage(Log::Type type, Log::Level level, const std::string_view format, Args&&... args)
#endif
	{
		auto detail = s_EnabledTags[""];
		if (detail.Enabled && detail.LevelFilter <= level)
		{
			auto logger = (type == Type::Core) ? GetCoreLogger() : GetClientLogger();
			switch (level)
			{
			case Level::Trace:
				logger->trace(format, std::forward<Args>(args)...);
				break;
			case Level::Info:
				logger->info(format, std::forward<Args>(args)...);
				break;
			case Level::Warn:
				logger->warn(format, std::forward<Args>(args)...);
				break;
			case Level::Error:
				logger->error(format, std::forward<Args>(args)...);
				break;
			case Level::Fatal:
				logger->critical(format, std::forward<Args>(args)...);
				break;
			}
		}
	}


	template<typename... Args>
	void Log::PrintMessageTag(Log::Type type, Log::Level level, std::string_view tag, const std::format_string<Args...> format, Args&&... args)
	{
		auto detail = s_EnabledTags[std::string(tag)];
		if (detail.Enabled && detail.LevelFilter <= level)
		{
			auto logger = (type == Type::Core) ? GetCoreLogger() : GetClientLogger();
			std::string formatted = std::format(format, std::forward<Args>(args)...);
			switch (level)
			{
				case Level::Trace:
					logger->trace("[{0}] {1}", tag, formatted);
					break;
				case Level::Info:
					logger->info("[{0}] {1}", tag, formatted);
					break;
				case Level::Warn:
					logger->warn("[{0}] {1}", tag, formatted);
					break;
				case Level::Error:
					logger->error("[{0}] {1}", tag, formatted);
					break;
				case Level::Fatal:
					logger->critical("[{0}] {1}", tag, formatted);
					break;
			}
		}
	}


	inline void Log::PrintMessageTag(Log::Type type, Log::Level level, std::string_view tag, std::string_view message)
	{
		auto detail = s_EnabledTags[std::string(tag)];
		if (detail.Enabled && detail.LevelFilter <= level)
		{
			auto logger = (type == Type::Core) ? GetCoreLogger() : GetClientLogger();
			switch (level)
			{
				case Level::Trace:
					logger->trace("[{0}] {1}", tag, message);
					break;
				case Level::Info:
					logger->info("[{0}] {1}", tag, message);
					break;
				case Level::Warn:
					logger->warn("[{0}] {1}", tag, message);
					break;
				case Level::Error:
					logger->error("[{0}] {1}", tag, message);
					break;
				case Level::Fatal:
					logger->critical("[{0}] {1}", tag, message);
					break;
			}
		}
	}


	template<typename... Args>
	void Log::PrintAssertMessage(Log::Type type, std::string_view prefix, std::format_string<Args...> message, Args&&... args)
	{
		auto logger = (type == Type::Core) ? GetCoreLogger() : GetClientLogger();
		auto formatted = std::format(message, std::forward<Args>(args)...);
		logger->error("{0}: {1}", prefix, formatted);

#if LUX_ASSERT_MESSAGE_BOX
		MessageBoxA(nullptr, formatted.data(), "Lux Assert", MB_OK | MB_ICONERROR);
#endif
	}


	inline void Log::PrintAssertMessage(Log::Type type, std::string_view prefix)
	{
		auto logger = (type == Type::Core) ? GetCoreLogger() : GetClientLogger();
		logger->error("{0}", prefix);
#if LUX_ASSERT_MESSAGE_BOX
		MessageBoxA(nullptr, "No message :(", "Lux Assert", MB_OK | MB_ICONERROR);
#endif
	}
}
