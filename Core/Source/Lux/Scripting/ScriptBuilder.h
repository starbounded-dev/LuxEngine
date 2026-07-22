#pragma once

#include <filesystem>
#include <string>

namespace Lux {

	// Builds the user's C# script project. SDK-style net9.0 projects are built with the dotnet
	// CLI (dotnet must be on PATH; the .NET 9 SDK is a prerequisite of the engine). Mirrors
	// Hazel's ScriptBuilder, but shells `dotnet build` rather than a hardcoded MSBuild path.
	class ScriptBuilder
	{
	public:
		static bool BuildProject(const std::filesystem::path& projectPath, const std::string& configuration = "Debug");
	};

}
