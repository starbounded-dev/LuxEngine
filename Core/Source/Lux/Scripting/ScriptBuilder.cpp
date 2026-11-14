// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ScriptBuilder.h"
#include "Lux/Utilities/FileSystem.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace Lux {

	bool ScriptBuilder::BuildProject(const std::filesystem::path& projectPath, const std::string& configuration)
	{
		if (!std::filesystem::exists(projectPath))
		{
			LUX_CORE_ERROR("[ScriptBuilder] Script project not found: {}", projectPath.string());
			return false;
		}

		// Projects created before the .NET 10 move pin net9.0 and cannot reference the net10 ScriptCore.dll.
		{
			static constexpr const char* s_Old = "<TargetFramework>net9.0</TargetFramework>";
			std::ifstream in(projectPath, std::ios::binary);
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();
			if (const size_t pos = text.find(s_Old); pos != std::string::npos)
			{
				text.replace(pos, std::strlen(s_Old), "<TargetFramework>net10.0</TargetFramework>");
				std::ofstream out(projectPath, std::ios::binary | std::ios::trunc);
				out.write(text.data(), (std::streamsize)text.size());
				if (out.good())
					LUX_CORE_INFO("[ScriptBuilder] Migrated {} from net9.0 to net10.0.", projectPath.filename().string());
				else
					LUX_CORE_ERROR("[ScriptBuilder] Could not migrate {} to net10.0; building as-is.", projectPath.string());
			}
		}

		// Invoke dotnet directly. std::system routes through cmd.exe on Windows and /bin/sh on
		// Linux; double-quoting the (possibly spaced) path is honoured by both, whereas a PowerShell
		// wrapper is Windows-only.
#ifdef LUX_PLATFORM_WINDOWS
		const std::string project = "\"" + projectPath.string() + "\""; // cmd.exe keeps & and spaces literal in quotes
#else
		const std::string project = FileSystem::QuoteShellArgument(projectPath); // /bin/sh expands $() in double quotes
#endif
		std::string command =
			"dotnet build " + project
			+ " -c " + configuration
			+ " --nologo --verbosity minimal";

		LUX_CORE_INFO("[ScriptBuilder] Building scripts ({})...", configuration);
		int result = std::system(command.c_str());
		if (result != 0)
		{
			LUX_CORE_ERROR("[ScriptBuilder] Script build failed with exit code {}.", result);
			return false;
		}

		return true;
	}

}
