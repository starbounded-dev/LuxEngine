// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>
#include <string_view>

namespace Lux {

	// Starts Lux-Watchdog.exe (built next to the editor) to watch this process from outside. On an
	// unhandled exception the watchdog writes a dump of this process; when the main loop stops for
	// Watchdog::HangSeconds it writes a hang dump; when the process ends without MarkCleanExit it
	// writes a report folder (dump, report.txt, the session's logs) and shows a short dialog.
	//
	// Skipped under a debugger, when LUX_NO_WATCHDOG is set (automated runs), when the watchdog
	// executable is missing, and on platforms other than Windows. Every call is a no-op then.
	class CrashWatchdog
	{
	public:
		static void Start(std::wstring_view appName, const std::filesystem::path& reportDirectory, const std::filesystem::path& logDirectory);
		static bool IsActive();

		// Main thread, once per main-loop iteration. Also re-installs the crash handler, which a
		// runtime started later (.NET) may have replaced.
		static void Heartbeat();
		// On while the main loop runs; off during startup and shutdown, which may take long.
		static void SetHangDetection(bool enabled);
		// The last thing before a normal exit.
		static void MarkCleanExit();
	};

}
