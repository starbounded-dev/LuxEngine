// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Utilities/Process.h"

#include <Windows.h>

#include <algorithm>

namespace Lux
{
	namespace
	{
		std::wstring ToWide(std::string_view text)
		{
			if (text.empty())
				return {};
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
			return wide;
		}

		// The MSVCRT argv rules: quote what has a space or a quote, and double the backslashes before a quote.
		void AppendArgument(std::wstring& commandLine, const std::wstring& argument)
		{
			if (!commandLine.empty())
				commandLine += L' ';
			if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
			{
				commandLine += argument;
				return;
			}
			commandLine += L'"';
			for (size_t i = 0;; ++i)
			{
				size_t backslashes = 0;
				while (i < argument.size() && argument[i] == L'\\')
				{
					++i;
					++backslashes;
				}
				if (i == argument.size())
				{
					commandLine.append(backslashes * 2, L'\\');
					break;
				}
				if (argument[i] == L'"')
					commandLine.append(backslashes * 2 + 1, L'\\');
				else
					commandLine.append(backslashes, L'\\');
				commandLine += argument[i];
			}
			commandLine += L'"';
		}
	}

	ProcessResult Process::Run(const ProcessOptions& options)
	{
		ProcessResult result;
		if (options.Arguments.empty())
		{
			result.Error = "no program to run";
			return result;
		}

		SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE readEnd = nullptr, writeEnd = nullptr;
		if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
		{
			result.Error = "could not create a pipe";
			return result;
		}
		SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
		HANDLE nulInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr);

		std::wstring commandLine;
		for (const std::string& argument : options.Arguments)
			AppendArgument(commandLine, ToWide(argument));

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = nulInput != INVALID_HANDLE_VALUE ? nulInput : nullptr;
		startup.hStdOutput = writeEnd;
		startup.hStdError = writeEnd;

		const std::wstring directory = options.WorkingDirectory.empty() ? std::wstring() : options.WorkingDirectory.wstring();
		PROCESS_INFORMATION process{};
		const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
			directory.empty() ? nullptr : directory.c_str(), &startup, &process);
		CloseHandle(writeEnd); // the child holds the only writer: its end closes the pipe
		if (nulInput != INVALID_HANDLE_VALUE)
			CloseHandle(nulInput);
		if (!started)
		{
			CloseHandle(readEnd);
			result.Error = "could not start " + options.Arguments.front() + " (error " + std::to_string(GetLastError()) + ")";
			return result;
		}
		result.Started = true;

		std::string pending;
		const auto flush = [&](bool all)
		{
			size_t begin = 0;
			for (size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', begin))
			{
				std::string_view line(pending.data() + begin, end - begin);
				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);
				if (options.OnLine)
					options.OnLine(line);
				begin = end + 1;
			}
			pending.erase(0, begin);
			if (all && !pending.empty())
			{
				if (options.OnLine)
					options.OnLine(pending);
				pending.clear();
			}
		};

		char buffer[4096];
		for (;;)
		{
			DWORD available = 0;
			if (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available > 0)
			{
				DWORD read = 0;
				if (ReadFile(readEnd, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) && read > 0)
				{
					pending.append(buffer, read);
					flush(false);
				}
				continue;
			}

			if (options.Cancel && options.Cancel->load(std::memory_order_relaxed))
			{
				TerminateProcess(process.hProcess, 1);
				result.Cancelled = true;
				WaitForSingleObject(process.hProcess, 5000);
				break;
			}
			if (WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0)
			{
				// Whatever the program wrote just before it ended.
				DWORD left = 0;
				while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &left, nullptr) && left > 0)
				{
					DWORD read = 0;
					if (!ReadFile(readEnd, buffer, std::min<DWORD>(left, sizeof(buffer)), &read, nullptr) || read == 0)
						break;
					pending.append(buffer, read);
				}
				break;
			}
		}
		flush(true);

		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		result.ExitCode = static_cast<int>(exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		CloseHandle(readEnd);
		return result;
	}
}
