#pragma once

#include "Lux/Project/Project.h"
#include "Lux/Scripting/ScriptBuilder.h"

#include <filesystem>
#include <initializer_list>
#include <string>

namespace Lux::RuntimeExport {

	inline constexpr const char* RuntimeProjectFile = "Project.luxruntime";
	inline constexpr const char* RuntimeAssetPackFile = "AssetPack.lap";
	inline constexpr const char* RuntimeShaderPackFile = "ShaderPack.lsp";

	inline constexpr const char* PlatformSuffix =
#ifdef LUX_PLATFORM_LINUX
		"-linux-x86_64";
#else
		"-windows-x86_64";
#endif

	inline constexpr const char* PlatformExportLabel =
#ifdef LUX_PLATFORM_LINUX
		"-Linux-x86_64";
#else
		"-Windows-x86_64";
#endif

	inline constexpr const char* RuntimeExeName =
#ifdef LUX_PLATFORM_LINUX
		"Lux-Runtime";
#else
		"Lux-Runtime.exe";
#endif

	bool FileExists(const std::filesystem::path& path);
	std::string SanitizeBuildName(std::string value);

	bool CopyFileIfExists(const std::filesystem::path& source, const std::filesystem::path& destination, bool required = false);
	bool CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination, bool skipDebugFiles = false);
	std::filesystem::path FindFirstExistingDirectory(std::initializer_list<std::filesystem::path> candidates);

	std::filesystem::path FindRepositoryRootFrom(std::filesystem::path start);
	std::filesystem::path GetRuntimeOutputDirectory(RuntimeExportTarget target);
	bool IsBuildConfigurationDirectory(const std::filesystem::path& path);
	std::filesystem::path GetRuntimeExecutablePath(RuntimeExportTarget target);
	bool IsRuntimeExecutableOutdated(const std::filesystem::path& runtimeExe, const std::filesystem::path& repositoryRoot);
	bool BuildRuntimeExecutable(RuntimeExportTarget target);

	std::filesystem::path ResolveScriptProjectFile(Ref<Project> project);
	bool IsScriptModuleOutdated(const std::filesystem::path& scriptModule, const std::filesystem::path& scriptProject);
	bool BuildScriptModule(RuntimeExportTarget target);

}
