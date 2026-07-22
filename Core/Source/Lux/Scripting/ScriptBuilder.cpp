#include "lpch.h"
#include "ScriptBuilder.h"

#include <cstdlib>

namespace Lux {

	bool ScriptBuilder::BuildProject(const std::filesystem::path& projectPath, const std::string& configuration)
	{
		if (!std::filesystem::exists(projectPath))
		{
			LUX_CORE_ERROR("[ScriptBuilder] Script project not found: {}", projectPath.string());
			return false;
		}

		// Invoke dotnet directly. std::system routes through cmd.exe on Windows and /bin/sh on
		// Linux; double-quoting the (possibly spaced) path is honoured by both, whereas a PowerShell
		// wrapper is Windows-only.
		std::string command =
			"dotnet build \"" + projectPath.string() + "\""
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
