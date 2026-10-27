// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// What a watched process (the editor) and Lux-Watchdog.exe share. Plain C++ and no engine headers:
// the watchdog is a separate, dependency-free program that must keep working while the engine
// process is crashing. The watched process creates the mapping and the two events, then starts
// the watchdog with its process ID; both sides derive the object names from that ID.

#include <cstdint>
#include <string>

namespace Lux::Watchdog {

	constexpr uint32_t Magic = 0x4457584C;   // "LXWD"
	constexpr uint32_t Version = 1;

	// The main loop has not advanced for this long: save a hang dump (once per stall).
	constexpr uint32_t HangSeconds = 30;
	// How long a crashing process waits for its dump before it terminates anyway.
	constexpr uint32_t DumpWaitMilliseconds = 60000;
	// Report folders kept; older ones are deleted when a new one is written.
	constexpr uint32_t ReportsKept = 10;

	enum State : int32_t
	{
		Running = 0,
		CleanExit = 1,   // set as the last thing before a normal exit; anything else is a crash
		Crashed = 2,     // the exception fields below are valid
	};

	struct Shared
	{
		uint32_t Magic;
		uint32_t Version;
		volatile int64_t Heartbeat;        // advanced once per main-loop iteration
		volatile int32_t State;
		volatile int32_t HangDetection;    // 1 while the main loop runs (not during startup or shutdown)
		uint32_t CrashThreadID;
		uint32_t ExceptionCode;
		uint64_t ExceptionPointers;        // EXCEPTION_POINTERS*, an address in the watched process
		wchar_t AppName[64];
		wchar_t LogDirectory[520];         // the session's logs end in _<pid>.log
		wchar_t ReportDirectory[520];      // report folders go here
	};

	inline std::wstring MappingName(uint32_t processID) { return L"Local\\LuxWatchdog-" + std::to_wstring(processID); }
	inline std::wstring CrashEventName(uint32_t processID) { return MappingName(processID) + L"-crash"; }
	inline std::wstring DumpedEventName(uint32_t processID) { return MappingName(processID) + L"-dumped"; }

	constexpr const wchar_t* ExecutableName = L"Lux-Watchdog.exe";

}
