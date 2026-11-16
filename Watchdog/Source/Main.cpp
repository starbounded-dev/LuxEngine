// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Lux-Watchdog: watches one engine process from outside and reports how it ended.
//
//   Lux-Watchdog.exe <process id>
//
// The watched process starts it (see Core/Source/Lux/Debug/CrashWatchdog.cpp) and shares the state in
// CrashWatchdogProtocol.h. A dump has to be written from outside: a crashing process cannot be
// trusted to walk its own memory. Depends on nothing but Windows, so it keeps working whatever state
// the engine is in.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>
#include <Psapi.h>
#include <shellapi.h>

#include "Lux/Debug/CrashWatchdogProtocol.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace Lux;

namespace {

	// Owns a handle. Move-only: a copy would close the handle under the other owner.
	struct Handle
	{
		HANDLE Value = nullptr;

		Handle() = default;
		explicit Handle(HANDLE value) : Value(value) {}
		Handle(Handle&& other) noexcept : Value(std::exchange(other.Value, nullptr)) {}
		Handle& operator=(Handle&& other) noexcept { std::swap(Value, other.Value); return *this; }
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		~Handle() { if (Value && Value != INVALID_HANDLE_VALUE) CloseHandle(Value); }

		operator HANDLE() const { return Value; }
	};

	std::wstring Timestamp(const char* format)
	{
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_s(&local, &now);
		char buffer[64];
		std::strftime(buffer, sizeof(buffer), format, &local);
		return std::wstring(buffer, buffer + std::strlen(buffer));
	}

	std::string Narrow(const std::wstring& text)
	{
		if (text.empty())
			return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		std::string result(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), size, nullptr, nullptr);
		return result;
	}

	std::string Hex(uint64_t value)
	{
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
		return buffer;
	}

	const char* DescribeCode(DWORD code)
	{
		switch (code)
		{
			case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
			case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
			case EXCEPTION_BREAKPOINT:            return "breakpoint (a failed assert)";
			case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
			case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
			case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
			case 0xC0000374:                      return "heap corruption";
			case 0xC0000409:                      return "fail-fast (stack buffer overrun or __fastfail)";
			case 0xE06D7363:                      return "uncaught C++ exception";
			case 0xE0004C01:                      return "abort() or std::terminate";
			case 0xE0004C02:                      return "pure virtual call";
			case 0xE0004C03:                      return "invalid CRT parameter";
			default:                              return "";
		}
	}

	// "Core.dll+0x1234" for an address in the watched process.
	std::string DescribeAddress(HANDLE process, uint64_t address)
	{
		HMODULE modules[1024];
		DWORD needed = 0;
		if (EnumProcessModulesEx(process, modules, sizeof(modules), &needed, LIST_MODULES_ALL))
		{
			const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), (DWORD)std::size(modules));
			for (DWORD i = 0; i < count; i++)
			{
				MODULEINFO info{};
				if (!GetModuleInformation(process, modules[i], &info, sizeof(info)))
					continue;
				const uint64_t base = reinterpret_cast<uint64_t>(info.lpBaseOfDll);
				if (address < base || address >= base + info.SizeOfImage)
					continue;
				wchar_t name[MAX_PATH]{};
				GetModuleBaseNameW(process, modules[i], name, MAX_PATH);
				return Narrow(name) + "+" + Hex(address - base);
			}
		}
		return Hex(address);
	}

	struct Watched
	{
		DWORD ID = 0;
		Handle Process;
		Handle Mapping;
		Watchdog::Shared* Shared = nullptr;
		Handle CrashEvent;
		Handle DumpedEvent;
		std::wstring AppName;
	};

	// One folder per incident: a hang dump and the exit that may follow it share one.
	class Report
	{
	public:
		explicit Report(const Watched& watched) : m_Watched(watched) {}

		const fs::path& Folder()
		{
			if (m_Folder.empty())
			{
				const fs::path root = m_Watched.Shared->ReportDirectory;
				m_Folder = root / (m_Watched.AppName + L"-" + Timestamp("%Y-%m-%d_%H-%M-%S"));
				std::error_code error;
				fs::create_directories(m_Folder, error);
				Prune(root);
			}
			return m_Folder;
		}

		bool Exists() const { return !m_Folder.empty(); }

		// exception: EXCEPTION_POINTERS* in the watched process, or 0 for a dump of a live process.
		bool WriteDump(const wchar_t* name, uint64_t exception, DWORD threadID)
		{
			const fs::path path = Folder() / name;
			Handle file{ CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
			if (file.Value == INVALID_HANDLE_VALUE)
			{
				file.Value = nullptr;
				return false;
			}
			MINIDUMP_EXCEPTION_INFORMATION info{};
			info.ThreadId = threadID;
			info.ExceptionPointers = reinterpret_cast<EXCEPTION_POINTERS*>(exception);
			info.ClientPointers = TRUE;   // the pointer is in the watched process
			const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo
				| MiniDumpWithUnloadedModules | MiniDumpWithHandleData | MiniDumpScanMemory);
			const BOOL written = MiniDumpWriteDump(m_Watched.Process, m_Watched.ID, file, type, exception ? &info : nullptr, nullptr, nullptr);
			if (written)
				m_Lines.push_back("Dump: " + Narrow(name));
			else
				m_Lines.push_back("Dump: could not be written (error " + std::to_string(GetLastError()) + ")");
			return written;
		}

		void Line(std::string line) { m_Lines.push_back(std::move(line)); }

		// Copies the session's logs (they end in _<pid>.log) and writes report.txt.
		void Finish(const std::string& headline)
		{
			const fs::path folder = Folder();
			std::vector<fs::path> logs;
			std::error_code error;
			const std::wstring suffix = L"_" + std::to_wstring(m_Watched.ID) + L".log";
			for (const auto& entry : fs::directory_iterator(m_Watched.Shared->LogDirectory, error))
			{
				const std::wstring name = entry.path().filename().wstring();
				if (name.size() > suffix.size() && name.ends_with(suffix))
				{
					fs::copy_file(entry.path(), folder / entry.path().filename(), fs::copy_options::overwrite_existing, error);
					logs.push_back(entry.path());
				}
			}

			std::ofstream out(folder / "report.txt", std::ios::app);
			out << headline << "\n\n";
			out << "App: " << Narrow(m_Watched.AppName) << "\n";
			out << "Process: " << m_Watched.ID << "\n";
			out << "Time: " << Narrow(Timestamp("%Y-%m-%d %H:%M:%S")) << "\n";
			for (const std::string& line : m_Lines)
				out << line << "\n";
			m_Lines.clear();

			// The engine log's last lines, so the report reads on its own.
			auto engineLog = std::find_if(logs.begin(), logs.end(), [](const fs::path& p) { return p.filename().wstring().starts_with(L"LUX_"); });
			if (engineLog != logs.end())
			{
				std::ifstream in(*engineLog);
				std::vector<std::string> tail;
				for (std::string line; std::getline(in, line);)
				{
					tail.push_back(std::move(line));
					if (tail.size() > 60)
						tail.erase(tail.begin());
				}
				out << "\nLast lines of " << engineLog->filename().string() << ":\n";
				for (const std::string& line : tail)
					out << "  " << line << "\n";
			}
			out << "\n";
		}

	private:
		// Keep the newest ReportsKept folders; names sort by time.
		void Prune(const fs::path& root)
		{
			std::vector<fs::path> folders;
			std::error_code error;
			for (const auto& entry : fs::directory_iterator(root, error))
			{
				if (entry.is_directory(error))
					folders.push_back(entry.path());
			}
			std::sort(folders.begin(), folders.end());
			while (folders.size() > Watchdog::ReportsKept)
			{
				fs::remove_all(folders.front(), error);
				folders.erase(folders.begin());
			}
		}

		const Watched& m_Watched;
		fs::path m_Folder;
		std::vector<std::string> m_Lines;
	};

	void ShowReport(const Watched& watched, const fs::path& folder, const std::wstring& what)
	{
		// Automated runs (and the watchdog's own tests) want the report without a dialog.
		if (_wgetenv(L"LUX_WATCHDOG_QUIET"))
			return;
		const std::wstring message = watched.AppName + L" " + what + L".\n\nA report was saved to:\n" + folder.wstring()
			+ L"\n\nIt holds a crash dump and the session's logs. Open the folder?";
		const std::wstring title = L"LuxEngine " + watched.AppName;
		if (MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_YESNO | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND) == IDYES)
			ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	std::optional<Watched> Attach(DWORD processID)
	{
		Watched watched;
		watched.ID = processID;
		watched.Process.Value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE | PROCESS_TERMINATE, FALSE, processID);
		watched.Mapping.Value = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, Watchdog::MappingName(processID).c_str());
		watched.CrashEvent.Value = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, Watchdog::CrashEventName(processID).c_str());
		watched.DumpedEvent.Value = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, Watchdog::DumpedEventName(processID).c_str());
		if (!watched.Process || !watched.Mapping || !watched.CrashEvent || !watched.DumpedEvent)
			return std::nullopt;
		watched.Shared = static_cast<Watchdog::Shared*>(MapViewOfFile(watched.Mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Watchdog::Shared)));
		if (!watched.Shared || watched.Shared->Magic != Watchdog::Magic || watched.Shared->Version != Watchdog::Version)
			return std::nullopt;
		watched.AppName = watched.Shared->AppName;
		return watched;
	}

	int Watch(Watched& watched)
	{
		std::optional<Report> report;
		report.emplace(watched);
		int64_t lastHeartbeat = watched.Shared->Heartbeat;
		auto lastProgress = std::chrono::steady_clock::now();
		bool hangReported = false;

		for (;;)
		{
			const HANDLE waits[] = { watched.Process, watched.CrashEvent };
			const DWORD signalled = WaitForMultipleObjects(2, waits, FALSE, 1000);
			if (signalled == WAIT_FAILED)
				return 4;   // the handles are unusable; never spin

			if (signalled == WAIT_OBJECT_0 + 1)
			{
				// Crashed: the process waits in its filter while we read its memory.
				const uint64_t exception = watched.Shared->ExceptionPointers;
				const DWORD code = watched.Shared->ExceptionCode;
				std::string where;
				EXCEPTION_POINTERS pointers{};
				EXCEPTION_RECORD record{};
				SIZE_T read = 0;
				if (ReadProcessMemory(watched.Process, reinterpret_cast<void*>(exception), &pointers, sizeof(pointers), &read)
					&& ReadProcessMemory(watched.Process, pointers.ExceptionRecord, &record, sizeof(record), &read))
					where = " at " + DescribeAddress(watched.Process, reinterpret_cast<uint64_t>(record.ExceptionAddress));

				const std::string name = DescribeCode(code);
				report->Line("Exception: " + Hex(code) + (name.empty() ? "" : " (" + name + ")") + where);
				report->Line("Thread: " + std::to_string(watched.Shared->CrashThreadID));
				report->WriteDump((watched.AppName + L".dmp").c_str(), exception, watched.Shared->CrashThreadID);
				SetEvent(watched.DumpedEvent);

				WaitForSingleObject(watched.Process, 15000);
				report->Finish("LuxEngine crash report: " + Narrow(watched.AppName) + " crashed");
				ShowReport(watched, report->Folder(), L"crashed");
				return 0;
			}

			if (signalled == WAIT_OBJECT_0)
			{
				DWORD exitCode = 0;
				GetExitCodeProcess(watched.Process, &exitCode);
				// A normal exit, or one through exit(0) somewhere outside the main path.
				if (watched.Shared->State == Watchdog::CleanExit || (exitCode == 0 && !report->Exists()))
					return 0;

				const std::string name = DescribeCode(exitCode);
				report->Line("Exit code: " + Hex(exitCode) + (name.empty() ? "" : " (" + name + ")"));
				if (!report->Exists())
					report->Line("Dump: none. The process ended without an unhandled exception the watchdog could catch "
						"(fail-fast and stack overflows end a process at once, as do Task Manager and debuggers).");
				report->Finish("LuxEngine crash report: " + Narrow(watched.AppName) + " ended unexpectedly");
				ShowReport(watched, report->Folder(), hangReported ? L"stopped responding and was closed" : L"ended unexpectedly");
				return 0;
			}

			// Hang check: the heartbeat must move while hang detection is on.
			const int64_t heartbeat = watched.Shared->Heartbeat;
			const auto now = std::chrono::steady_clock::now();
			if (heartbeat != lastHeartbeat || !watched.Shared->HangDetection)
			{
				lastHeartbeat = heartbeat;
				lastProgress = now;
				if (hangReported)
					report.emplace(watched);   // recovered: a later incident gets its own folder
				hangReported = false;
				continue;
			}
			if (!hangReported && now - lastProgress >= std::chrono::seconds(Watchdog::HangSeconds))
			{
				// No dialog: it may still recover. The report is there if it does not.
				hangReported = true;
				report->Line("Stopped responding: the main loop did not advance for " + std::to_string(Watchdog::HangSeconds) + " s.");
				report->WriteDump((watched.AppName + L"-hang.dmp").c_str(), 0, 0);
				report->Finish("LuxEngine hang report: " + Narrow(watched.AppName) + " stopped responding");
			}
		}
	}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
	const DWORD processID = static_cast<DWORD>(std::wcstoul(commandLine, nullptr, 10));
	if (processID == 0)
		return 2;
	std::optional<Watched> watched = Attach(processID);
	if (!watched)
		return 3;
	return Watch(*watched);
}
