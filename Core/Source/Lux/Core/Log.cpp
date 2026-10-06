// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Log.h"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "Lux/Editor/EditorConsole/EditorConsoleSink.h"
#include "Lux/Utilities/StringUtils.h"

#include <algorithm>
#include <filesystem>
#include <format>

#ifndef LUX_PLATFORM_WINDOWS
	#include <unistd.h>
#endif

#define LUX_HAS_CONSOLE !LUX_DIST

namespace Lux {

	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger;

	std::map<std::string, Log::TagDetails> Log::s_DefaultTagDetails = {
		{ "",                  TagDetails{  true, Level::Trace } },
		{ "Animation",         TagDetails{  true, Level::Warn  } },
		{ "Asset Pack",        TagDetails{  true, Level::Warn  } },
		{ "AssetManager",      TagDetails{  true, Level::Info  } },
		{ "AssetSystem",       TagDetails{  true, Level::Info  } },
		{ "Assimp",            TagDetails{  true, Level::Error } },
		{ "Audio",             TagDetails{  true, Level::Error } },
		{ "Core",              TagDetails{  true, Level::Trace } },
		{ "GLFW",              TagDetails{  true, Level::Error } },
		{ "Memory",            TagDetails{  true, Level::Error } },
		{ "Mesh",              TagDetails{  true, Level::Warn  } },
		{ "Physics",           TagDetails{  true, Level::Warn  } },
		{ "Project",           TagDetails{  true, Level::Warn  } },
		{ "Renderer",          TagDetails{  true, Level::Info  } },
		{ "Scene",             TagDetails{  true, Level::Info  } },
		{ "Scripting",         TagDetails{  true, Level::Warn  } },
		{ "Sound Spatializer", TagDetails{  true, Level::Warn  } },
		{ "Timer",             TagDetails{ false, Level::Trace } },
	};

	// Each run logs to its own files (LUX_<date>_<time>_<pid>.log), so two editors open at once don't
	// truncate each other's logs. Only the newest sessions are kept.
	static constexpr size_t s_MaxLogSessions = 10;

	static uint32_t GetCurrentProcessID()
	{
#ifdef LUX_PLATFORM_WINDOWS
		return static_cast<uint32_t>(::GetCurrentProcessId());
#else
		return static_cast<uint32_t>(::getpid());
#endif
	}

	// Deletes all but the newest `keepCount` "<prefix>_*.log" files. Names start with a sortable
	// timestamp, so name order is age order. Failures are ignored: on Windows a log still held open
	// by another running instance cannot be deleted, and that is fine.
	static void RemoveOldLogSessions(const std::filesystem::path& logDirectory, std::string_view prefix, size_t keepCount)
	{
		std::vector<std::filesystem::path> sessionLogs;
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(logDirectory, error))
		{
			const std::string fileName = entry.path().filename().string();
			if (entry.is_regular_file(error) && fileName.starts_with(prefix) && fileName.size() > prefix.size() && fileName[prefix.size()] == '_' && fileName.ends_with(".log"))
				sessionLogs.push_back(entry.path());
		}

		if (sessionLogs.size() <= keepCount)
			return;

		std::sort(sessionLogs.begin(), sessionLogs.end());
		for (size_t i = 0; i < sessionLogs.size() - keepCount; i++)
			std::filesystem::remove(sessionLogs[i], error);
	}

	void Log::Init(const std::filesystem::path& logDirectory)
	{
		if (!std::filesystem::exists(logDirectory))
			std::filesystem::create_directories(logDirectory);
		s_LogDirectory = logDirectory;

		// Room for this session: it is created after the cleanup.
		RemoveOldLogSessions(logDirectory, "LUX", s_MaxLogSessions - 1);
		RemoveOldLogSessions(logDirectory, "APP", s_MaxLogSessions - 1);

		const std::string sessionSuffix = std::format("{}_{}.log", Utils::String::GetCurrentTimeString(true, true), GetCurrentProcessID());
		const std::string luxLogPath = (logDirectory / ("LUX_" + sessionSuffix)).string();
		const std::string appLogPath = (logDirectory / ("APP_" + sessionSuffix)).string();

		auto luxFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(luxLogPath, true);
		auto appFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(appLogPath, true);
		luxFileSink->set_pattern("[%T] [%l] %n: %v");
		appFileSink->set_pattern("[%T] [%l] %n: %v");

		// The editor-console logger shares the APP file sink: a second sink on the same file would
		// open (and truncate) it twice.
		std::vector<spdlog::sink_ptr> luxSinks = { luxFileSink };
		std::vector<spdlog::sink_ptr> appSinks = { appFileSink };
		std::vector<spdlog::sink_ptr> editorConsoleSinks = { appFileSink };

#if LUX_HAS_CONSOLE
		auto luxStdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		auto appStdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		auto consoleStdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		luxStdoutSink->set_pattern("%^[%T] %n: %v%$");
		appStdoutSink->set_pattern("%^[%T] %n: %v%$");
		consoleStdoutSink->set_pattern("%^%v%$");
		luxSinks.push_back(luxStdoutSink);
		appSinks.push_back(appStdoutSink);
		editorConsoleSinks.push_back(consoleStdoutSink);

		// One editor Log panel sink shared by every logger, so the panel mirrors the terminal:
		// anything that passes the tag/level filters and prints to stdout also lands in the panel.
		// The panel has its own time column, so the pattern is just the logger name and message.
		auto editorPanelSink = std::make_shared<EditorConsoleSink>(1);
		editorPanelSink->set_pattern("%n: %v");
		luxSinks.push_back(editorPanelSink);
		appSinks.push_back(editorPanelSink);
		editorConsoleSinks.push_back(editorPanelSink);
#endif

		s_CoreLogger = std::make_shared<spdlog::logger>("LUX", luxSinks.begin(), luxSinks.end());
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClientLogger = std::make_shared<spdlog::logger>("APP", appSinks.begin(), appSinks.end());
		s_ClientLogger->set_level(spdlog::level::trace);

		s_EditorConsoleLogger = std::make_shared<spdlog::logger>("Console", editorConsoleSinks.begin(), editorConsoleSinks.end());
		s_EditorConsoleLogger->set_level(spdlog::level::trace);

		SetDefaultTagSettings();
	}

	void Log::Shutdown()
	{
		s_EditorConsoleLogger.reset();
		s_ClientLogger.reset();
		s_CoreLogger.reset();
		spdlog::drop_all();
	}

	void Log::SetDefaultTagSettings()
	{
		s_EnabledTags = s_DefaultTagDetails;
	}

}
