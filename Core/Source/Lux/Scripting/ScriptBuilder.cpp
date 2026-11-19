// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ScriptBuilder.h"
#include "Lux/Utilities/Process.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace Lux {

	namespace
	{
		// Projects created before the .NET 10 move pin net9.0 and cannot reference the net10 ScriptCore.dll.
		void MigrateTargetFramework(const std::filesystem::path& projectPath)
		{
			static constexpr const char* s_Old = "<TargetFramework>net9.0</TargetFramework>";
			std::ifstream in(projectPath, std::ios::binary);
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();
			const size_t pos = text.find(s_Old);
			if (pos == std::string::npos)
				return;

			text.replace(pos, std::strlen(s_Old), "<TargetFramework>net10.0</TargetFramework>");
			std::ofstream out(projectPath, std::ios::binary | std::ios::trunc);
			out.write(text.data(), (std::streamsize)text.size());
			if (out.good())
				LUX_CORE_INFO_TAG("ScriptBuilder", "Migrated {} from net9.0 to net10.0.", projectPath.filename().string());
			else
				LUX_CORE_ERROR_TAG("ScriptBuilder", "Could not migrate {} to net10.0; building as-is.", projectPath.string());
		}
	}

	ScriptBuildResult ScriptBuilder::Build(const std::filesystem::path& projectPath, const std::string& configuration,
		const std::atomic<bool>* cancel, const std::function<void(std::string_view)>& onLine)
	{
		ScriptBuildResult result;
		if (!std::filesystem::exists(projectPath))
		{
			result.Error = "Script project not found: " + projectPath.string();
			LUX_CORE_ERROR_TAG("ScriptBuilder", "{}", result.Error);
			return result;
		}

		MigrateTargetFramework(projectPath);

		ProcessOptions process;
		process.Arguments = { "dotnet", "build", projectPath.string(), "-c", configuration, "--nologo", "--verbosity", "minimal" };
		process.WorkingDirectory = projectPath.parent_path();
		process.Cancel = cancel;
		process.OnLine = [&](std::string_view line)
		{
			result.Output.emplace_back(line);
			if (BuildDiagnostic diagnostic; ParseCompilerDiagnostic(line, diagnostic))
			{
				// MSBuild prints a problem as it happens and again in the summary: once is enough.
				const auto same = [&](const BuildDiagnostic& other)
				{
					return other.Location == diagnostic.Location && other.Code == diagnostic.Code && other.Message == diagnostic.Message;
				};
				if (std::none_of(result.Diagnostics.begin(), result.Diagnostics.end(), same))
					result.Diagnostics.push_back(std::move(diagnostic));
			}
			if (onLine)
				onLine(line);
		};

		LUX_CORE_INFO_TAG("ScriptBuilder", "Building scripts ({})...", configuration);
		const ProcessResult run = Process::Run(process);
		result.ExitCode = run.ExitCode;
		result.Cancelled = run.Cancelled;
		if (!run.Started)
		{
			result.Error = "Could not run dotnet (" + run.Error + "). The .NET 10 SDK must be installed and on the PATH.";
			LUX_CORE_ERROR_TAG("ScriptBuilder", "{}", result.Error);
			return result;
		}

		result.Success = !run.Cancelled && run.ExitCode == 0;
		if (run.Cancelled)
			LUX_CORE_WARN_TAG("ScriptBuilder", "Script build cancelled.");
		else if (!result.Success)
			LUX_CORE_ERROR_TAG("ScriptBuilder", "Script build failed with exit code {} ({} problem{} in files).", run.ExitCode, result.Diagnostics.size(), result.Diagnostics.size() == 1 ? "" : "s");
		return result;
	}

	bool ScriptBuilder::BuildProject(const std::filesystem::path& projectPath, const std::string& configuration)
	{
		const ScriptBuildResult result = Build(projectPath, configuration, nullptr, [](std::string_view line)
		{
			if (!line.empty())
				LUX_CORE_INFO_TAG("ScriptBuilder", "{}", line);
		});
		return result.Success;
	}

}
