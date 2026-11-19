// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Utilities/SourceLocation.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Lux {

	struct ScriptBuildResult
	{
		bool Success = false;
		bool Cancelled = false;
		int ExitCode = -1;
		std::string Error;                         // why the build could not start (no dotnet, no project), else empty
		std::vector<BuildDiagnostic> Diagnostics;  // errors and warnings that have a file and line, each once
		std::vector<std::string> Output;           // every line the compiler printed
	};

	// Builds the user's C# script project. SDK-style net10.0 projects are built with the dotnet
	// CLI (dotnet must be on PATH; the .NET 10 SDK is a prerequisite of the engine). Mirrors
	// Hazel's ScriptBuilder, but runs `dotnet build` through Lux::Process (no shell, no console window) and keeps what the
	// compiler said.
	class ScriptBuilder
	{
	public:
		// Builds and returns what happened. Blocks: call it from a worker thread. `onLine` sees each output line as it arrives;
		// setting `cancel` stops the build.
		static ScriptBuildResult Build(const std::filesystem::path& projectPath, const std::string& configuration = "Debug",
			const std::atomic<bool>* cancel = nullptr, const std::function<void(std::string_view)>& onLine = {});

		// Build for callers that only need to know whether it worked (the runtime export): logs the compiler's output.
		static bool BuildProject(const std::filesystem::path& projectPath, const std::string& configuration = "Debug");
	};

}
