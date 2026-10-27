// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Application.h"
#include "Lux/Core/Version.h"
#include "Lux/Debug/CrashWatchdog.h"
#include "Lux/Utilities/FileSystem.h"

#include <cstdio>
#include <string_view>

// Define LUX_LOGS_IN_PERSISTENT_STORAGE before including this header to write logs to the per-user
// folder (FileSystem::GetPersistentStoragePath()/logs) instead of ./logs. The editor does; shipped
// games (Lux-Runtime) keep their logs next to the game.
//
// Define LUX_CRASH_WATCHDOG as the app's name (a wide string literal, L"Editor") to have
// Lux-Watchdog.exe watch the process and write crash reports (see Lux/Debug/CrashWatchdog.h).

extern Lux::Application* Lux::CreateApplication(int argc, char** argv);
bool g_ApplicationRunning = true;

namespace Lux {

	int Main(int argc, char** argv)
	{
		// Before anything starts: no window, no log files.
		for (int i = 1; i < argc; i++)
		{
			if (std::string_view(argv[i]) == "--version")
			{
				std::puts(LUX_VERSION_LONG);
				return 0;
			}
		}

		while (g_ApplicationRunning)
		{
#ifdef LUX_LOGS_IN_PERSISTENT_STORAGE
			InitializeCore(FileSystem::GetPersistentStoragePath() / "logs");
#else
			InitializeCore();
#endif
#ifdef LUX_CRASH_WATCHDOG
			// Once per process, after logging exists (its session logs go into the reports).
			CrashWatchdog::Start(LUX_CRASH_WATCHDOG, FileSystem::GetPersistentStoragePath() / "CrashReports", Log::GetLogDirectory());
#endif
			Application* app = CreateApplication(argc, argv);
			LUX_CORE_ASSERT(app, "Client Application is null!");
			app->Run();
			delete app;
			ShutdownCore();
		}
		CrashWatchdog::MarkCleanExit();
		return 0;
	}

}

#if LUX_DIST && LUX_PLATFORM_WINDOWS

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
	return Lux::Main(__argc, __argv);
}

#else

int main(int argc, char** argv)
{
	return Lux::Main(argc, argv);
}

#endif // LUX_DIST
