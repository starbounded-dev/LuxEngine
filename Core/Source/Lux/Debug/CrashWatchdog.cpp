// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "CrashWatchdog.h"

#include "CrashWatchdogProtocol.h"

#ifdef LUX_PLATFORM_WINDOWS
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace Lux {

#ifdef LUX_PLATFORM_WINDOWS

	namespace {

		// Written once by Start on the main thread before any other function can see it, then read
		// by the crash handler on whichever thread crashes.
		Watchdog::Shared* s_Shared = nullptr;
		HANDLE s_CrashEvent = nullptr;
		HANDLE s_DumpedEvent = nullptr;
		HANDLE s_WatchdogProcess = nullptr;
		volatile LONG s_Crashing = 0;

		// Codes for the CRT failures that never reach the unhandled-exception filter on their own.
		constexpr DWORD AbortCode = 0xE0004C01;            // abort() / std::terminate
		constexpr DWORD PureCallCode = 0xE0004C02;
		constexpr DWORD InvalidParameterCode = 0xE0004C03;

		LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception)
		{
			// One report: a second thread crashing meanwhile just waits for the first to finish.
			if (InterlockedCompareExchange(&s_Crashing, 1, 0) != 0)
			{
				Sleep(Watchdog::DumpWaitMilliseconds);
				return EXCEPTION_EXECUTE_HANDLER;
			}

			s_Shared->CrashThreadID = GetCurrentThreadId();
			s_Shared->ExceptionCode = exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionCode : 0;
			s_Shared->ExceptionPointers = reinterpret_cast<uint64_t>(exception);
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&s_Shared->State), Watchdog::Crashed);
			SetEvent(s_CrashEvent);

			// The watchdog reads this process's memory while it dumps; stay alive until it is done
			// (or has died itself).
			const HANDLE waits[] = { s_DumpedEvent, s_WatchdogProcess };
			WaitForMultipleObjects(2, waits, FALSE, Watchdog::DumpWaitMilliseconds);

			// Best effort, after the dump: the log lines of the last second (info flushes once a
			// second), which the report copies once this process is gone. On another thread with a
			// time limit, in case the crash happened while a logger held its lock.
			if (HANDLE flusher = CreateThread(nullptr, 0, [](LPVOID) -> DWORD
				{
					spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger) { logger->flush(); });
					return 0;
				}, nullptr, 0, nullptr))
			{
				WaitForSingleObject(flusher, 2000);
				CloseHandle(flusher);
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

		void RaiseFatal(DWORD code)
		{
			// Through the filter above, so these failures get a dump with this thread's stack too.
			RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		}

		void InstallHandlers()
		{
			SetUnhandledExceptionFilter(OnUnhandledException);
		}

	}

	void CrashWatchdog::Start(std::wstring_view appName, const std::filesystem::path& reportDirectory, const std::filesystem::path& logDirectory)
	{
		if (s_Shared)
			return;
		if (IsDebuggerPresent())
			return;   // the debugger catches crashes, and breakpoints would read as hangs
		if (const char* disabled = std::getenv("LUX_NO_WATCHDOG"); disabled && *disabled && *disabled != '0')
			return;

		wchar_t modulePath[MAX_PATH]{};
		GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
		const std::filesystem::path watchdogPath = std::filesystem::path(modulePath).parent_path() / Watchdog::ExecutableName;
		std::error_code error;
		if (!std::filesystem::exists(watchdogPath, error))
		{
			LUX_CORE_WARN_TAG("Core", "Crash watchdog not started: {} is missing", watchdogPath.string());
			return;
		}

		const DWORD processID = GetCurrentProcessId();
		HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Watchdog::Shared),
			Watchdog::MappingName(processID).c_str());
		if (!mapping)
			return;
		auto* shared = static_cast<Watchdog::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Watchdog::Shared)));
		HANDLE crashEvent = CreateEventW(nullptr, FALSE, FALSE, Watchdog::CrashEventName(processID).c_str());
		HANDLE dumpedEvent = CreateEventW(nullptr, FALSE, FALSE, Watchdog::DumpedEventName(processID).c_str());
		if (!shared || !crashEvent || !dumpedEvent)
		{
			LUX_CORE_WARN_TAG("Core", "Crash watchdog not started: shared state could not be created ({})", GetLastError());
			return;
		}

		*shared = {};
		shared->Magic = Watchdog::Magic;
		shared->Version = Watchdog::Version;
		shared->State = Watchdog::Running;
		auto copy = [](wchar_t* target, size_t capacity, std::wstring_view text)
		{
			const size_t count = std::min(text.size(), capacity - 1);
			std::copy_n(text.data(), count, target);
			target[count] = L'\0';
		};
		copy(shared->AppName, std::size(shared->AppName), appName);
		copy(shared->LogDirectory, std::size(shared->LogDirectory), std::filesystem::absolute(logDirectory, error).wstring());
		copy(shared->ReportDirectory, std::size(shared->ReportDirectory), std::filesystem::absolute(reportDirectory, error).wstring());

		std::wstring commandLine = L"\"" + watchdogPath.wstring() + L"\" " + std::to_wstring(processID);
		STARTUPINFOW startup{ sizeof(startup) };
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(watchdogPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &process))
		{
			LUX_CORE_WARN_TAG("Core", "Crash watchdog not started: {} could not be launched ({})", watchdogPath.string(), GetLastError());
			return;
		}
		CloseHandle(process.hThread);

		s_Shared = shared;
		s_CrashEvent = crashEvent;
		s_DumpedEvent = dumpedEvent;
		s_WatchdogProcess = process.hProcess;
		// The mapping handle stays open for the life of the process, like the view.

		InstallHandlers();
		signal(SIGABRT, [](int) { RaiseFatal(AbortCode); });
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		_set_purecall_handler([] { RaiseFatal(PureCallCode); });
		_set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) { RaiseFatal(InvalidParameterCode); });

		LUX_CORE_INFO_TAG("Core", "Crash watchdog started; reports go to {}", reportDirectory.string());
	}

	bool CrashWatchdog::IsActive()
	{
		return s_Shared != nullptr;
	}

	void CrashWatchdog::Heartbeat()
	{
		if (!s_Shared)
			return;
		InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&s_Shared->Heartbeat));
		InstallHandlers();
	}

	void CrashWatchdog::SetHangDetection(bool enabled)
	{
		if (s_Shared)
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&s_Shared->HangDetection), enabled ? 1 : 0);
	}

	void CrashWatchdog::MarkCleanExit()
	{
		if (!s_Shared)
			return;
		InterlockedExchange(reinterpret_cast<volatile LONG*>(&s_Shared->HangDetection), 0);
		InterlockedExchange(reinterpret_cast<volatile LONG*>(&s_Shared->State), Watchdog::CleanExit);
	}

#else

	void CrashWatchdog::Start(std::wstring_view, const std::filesystem::path&, const std::filesystem::path&) {}
	bool CrashWatchdog::IsActive() { return false; }
	void CrashWatchdog::Heartbeat() {}
	void CrashWatchdog::SetHangDetection(bool) {}
	void CrashWatchdog::MarkCleanExit() {}

#endif

}
