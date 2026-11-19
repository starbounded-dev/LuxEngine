// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Lux
{
	struct ProcessOptions
	{
		std::vector<std::string> Arguments;               // the program first, found on PATH; UTF-8, never shell-quoted
		std::filesystem::path WorkingDirectory;           // empty: this process's
		std::function<void(std::string_view)> OnLine;     // each line of stdout and stderr, in order, without its line break
		const std::atomic<bool>* Cancel = nullptr;        // set to true to stop the process
	};

	struct ProcessResult
	{
		bool Started = false;
		bool Cancelled = false;
		int ExitCode = -1;
		std::string Error; // why it did not start
	};

	// Runs a program and waits for it, handing its output to OnLine as it arrives. No shell is involved, so a path with spaces or
	// shell characters is safe. Call it from a worker thread: it blocks until the program ends or Cancel is set. Windows and Linux.
	class Process
	{
	public:
		static ProcessResult Run(const ProcessOptions& options);
	};
}
