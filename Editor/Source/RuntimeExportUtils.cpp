// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "RuntimeExportUtils.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/Log.h"
#include "Lux/Audio/AudioBankBuilder.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>

namespace Lux::RuntimeExport {

	bool PrepareAudioBanks(const Project& project, AudioBankManifest& manifest)
	{
		manifest = {};
		const auto studio = project.GetStudioProjectPath();
		if (studio.empty())
			return true;
		const auto directory = project.GetStudioBankDirectory();
		std::error_code ec;
		if (!FileExists(studio) || !std::filesystem::is_directory(directory, ec))
		{
			LUX_CORE_ERROR_TAG("Audio", "Export requires Studio project '{0}' and built banks at '{1}'. Check Project Settings > Audio and build banks in Studio", studio.string(), directory.string());
			return false;
		}
		for (auto it = std::filesystem::directory_iterator(directory, ec);
			!ec && it != std::filesystem::directory_iterator(); it.increment(ec))
		{
			if (it->path().extension() != ".bank")
				continue;
			if (!it->is_regular_file(ec) || ec || it->file_size(ec) == 0 || ec)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot export empty or unreadable bank '{0}'", it->path().string());
				return false;
			}
			manifest.Banks.push_back(it->path().filename().string());
		}
		if (ec)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot enumerate export banks at '{0}': {1}", directory.string(), ec.message());
			return false;
		}
		std::sort(manifest.Banks.begin(), manifest.Banks.end());
		bool hasMaster = false;
		for (const auto& name : manifest.Banks)
		{
			const std::filesystem::path path(name);
			if (path.stem().extension() == ".strings")
			{
				const auto master = path.stem().stem().string() + ".bank";
				hasMaster = hasMaster || std::binary_search(manifest.Banks.begin(), manifest.Banks.end(), master);
			}
		}
		if (!hasMaster)
		{
			LUX_CORE_ERROR_TAG("Audio", "Export banks at '{0}' need a master bank and its matching .strings.bank. Build all banks in FMOD Studio", directory.string());
			return false;
		}
		if (AudioBankBuilder::NeedsRebuild(studio, directory))
		{
			LUX_CORE_ERROR_TAG("Audio", "Export banks at '{0}' are stale. Build all banks in FMOD Studio, then export again", directory.string());
			return false;
		}
		// Preserve asset-relative script paths. External authoring output gets a portable location.
		auto relative = std::filesystem::relative(directory, project.GetAssetDirectory(), ec);
		if (ec)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot resolve export bank directory '{0}': {1}", directory.string(), ec.message());
			return false;
		}
		bool external = relative.empty() || relative.has_root_path();
		for (const auto& part : relative)
			external = external || part == "..";
		manifest.Directory = external ? std::filesystem::path("Audio/Banks") : relative;
		manifest.EnableLiveUpdate = project.GetConfig().Audio.EnableLiveUpdate
			&& project.GetConfig().RuntimeExport.TargetConfig != RuntimeExportTarget::Dist;
		return manifest.Validate();
	}

	bool CopyAudioBanks(const Project& project, const AudioBankManifest& manifest, const std::filesystem::path& assets)
	{
		if (!manifest.Validate())
			return false;
		for (const auto& name : manifest.Banks)
		{
			if (!CopyFileIfExists(project.GetStudioBankDirectory() / name, assets / manifest.Directory / name, true))
				return false;
		}
		return true;
	}

	bool CopyAudioLibraries(const std::filesystem::path& runtimeDirectory, const std::filesystem::path& exportRoot)
	{
#ifdef LUX_PLATFORM_LINUX
		const auto source = runtimeDirectory / "lib";
		const auto destination = exportRoot / "lib";
		constexpr const char* libraries[] = { "libfmod.so.14", "libfmodstudio.so.14", "libvaudionative.so" };
#else
		const auto& source = runtimeDirectory;
		const auto& destination = exportRoot;
		constexpr const char* libraries[] = { "fmod.dll", "fmodstudio.dll", "vaudionative.dll" };
#endif
		for (const char* library : libraries)
		{
			if (!CopyFileIfExists(source / library, destination / library, true))
				return false;
		}
		return true;
	}

	bool FileExists(const std::filesystem::path& path)
	{
		std::error_code ec;
		return !path.empty() && std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
	}

	std::string SanitizeBuildName(std::string value)
	{
		if (value.empty())
			value = "LuxGame";

		for (char& c : value)
		{
			const bool valid = std::isalnum((unsigned char)c) || c == '-' || c == '_';
			if (!valid)
				c = '_';
		}

		return value;
	}

	bool CopyFileIfExists(const std::filesystem::path& source, const std::filesystem::path& destination, bool required)
	{
		std::error_code ec;
		if (!std::filesystem::exists(source, ec) || ec)
		{
			if (required)
				LUX_CONSOLE_LOG_ERROR("Missing export file: {}", source.string());
			return false;
		}

		std::filesystem::create_directories(destination.parent_path(), ec);
		if (ec)
		{
			LUX_CORE_ERROR_TAG("Project", "Cannot create export directory '{0}': {1}", destination.parent_path().string(), ec.message());
			return false;
		}
		std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			LUX_CONSOLE_LOG_ERROR("Failed to copy '{}' to '{}': {}", source.string(), destination.string(), ec.message());
			return false;
		}

		return true;
	}

	bool CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination, bool skipDebugFiles)
	{
		std::error_code ec;
		if (!std::filesystem::exists(source, ec) || ec)
			return false;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(source, ec))
		{
			if (ec)
				break;

			const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), source, ec);
			if (ec)
				continue;

			if (!relativePath.empty() && *relativePath.begin() == "Cache")
				continue;

			const std::filesystem::path target = destination / relativePath;
			if (entry.is_directory(ec))
			{
				std::filesystem::create_directories(target, ec);
				continue;
			}

			if (entry.is_regular_file(ec))
			{
				if (skipDebugFiles)
				{
					const std::filesystem::path extension = entry.path().extension();
					if (extension == ".pdb" || extension == ".ilk" || extension == ".exp")
						continue;
				}
				CopyFileIfExists(entry.path(), target);
			}
		}

		return true;
	}

	std::filesystem::path FindFirstExistingDirectory(std::initializer_list<std::filesystem::path> candidates)
	{
		std::error_code ec;
		for (const std::filesystem::path& candidate : candidates)
		{
			if (!candidate.empty() && std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec))
				return candidate;
		}

		return {};
	}

	std::filesystem::path FindRepositoryRootFrom(std::filesystem::path start)
	{
		if (start.empty())
			return {};

		std::error_code ec;
		start = std::filesystem::absolute(start, ec).lexically_normal();
		if (ec)
			return {};

		if (std::filesystem::is_regular_file(start, ec))
			start = start.parent_path();

		for (std::filesystem::path directory = start; !directory.empty(); directory = directory.parent_path())
		{
			if (std::filesystem::exists(directory / "premake5.lua", ec)
				&& std::filesystem::exists(directory / "Core", ec)
				&& std::filesystem::exists(directory / "Lux-Runtime" / "premake5.lua", ec))
			{
				return directory;
			}

			if (directory == directory.root_path())
				break;
		}

		return {};
	}

	std::filesystem::path GetRuntimeOutputDirectory(RuntimeExportTarget target)
	{
		return std::string(RuntimeExportTargetToString(target)) + PlatformSuffix;
	}

	bool IsBuildConfigurationDirectory(const std::filesystem::path& path)
	{
		const std::string directoryName = path.filename().string();
		return path.parent_path().filename() == "bin" && directoryName.find(PlatformSuffix) != std::string::npos;
	}

	std::filesystem::path GetRuntimeExecutablePath(RuntimeExportTarget target)
	{
		std::error_code ec;
		const std::filesystem::path current = std::filesystem::current_path(ec);
		if (ec)
			return {};

		const std::filesystem::path runtimeOutputDirectory = GetRuntimeOutputDirectory(target);
		std::vector<std::filesystem::path> candidates;

		if (std::filesystem::path root = FindRepositoryRootFrom(current); !root.empty())
			candidates.emplace_back((root / "bin" / runtimeOutputDirectory / "Lux-Runtime" / RuntimeExeName).lexically_normal());

		if (Ref<Project> activeProject = Project::GetActive())
		{
			if (std::filesystem::path root = FindRepositoryRootFrom(activeProject->GetProjectDirectory()); !root.empty())
				candidates.emplace_back((root / "bin" / runtimeOutputDirectory / "Lux-Runtime" / RuntimeExeName).lexically_normal());
		}

		const std::filesystem::path buildConfigDirectory = current.filename() == "Editor" ? current.parent_path() : current;
		if (IsBuildConfigurationDirectory(buildConfigDirectory))
			candidates.emplace_back((buildConfigDirectory / "Lux-Runtime" / RuntimeExeName).lexically_normal());

		for (const std::filesystem::path& candidate : candidates)
		{
			if (FileExists(candidate))
				return candidate;
		}

		return {};
	}

	bool IsRuntimeExecutableOutdated(const std::filesystem::path& runtimeExe, const std::filesystem::path& repositoryRoot)
	{
		std::error_code ec;
		if (runtimeExe.empty() || !std::filesystem::exists(runtimeExe, ec))
			return true;

		const auto executableWriteTime = std::filesystem::last_write_time(runtimeExe, ec);
		if (ec)
			return true;

		const std::array<std::filesystem::path, 2> sourceRoots = {
			repositoryRoot / "Lux-Runtime",
			repositoryRoot / "Core" / "Source"
		};

		for (const auto& sourceRoot : sourceRoots)
		{
			if (!std::filesystem::exists(sourceRoot, ec))
				continue;

			for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot, ec))
			{
				if (ec)
					break;
				if (!entry.is_regular_file(ec))
					continue;

				const std::filesystem::path extension = entry.path().extension();
				if (extension != ".cpp" && extension != ".h" && extension != ".hpp" && extension != ".c" && extension != ".rc" && extension != ".lua")
					continue;

				if (entry.last_write_time(ec) > executableWriteTime && !ec)
					return true;
			}
		}

		return false;
	}

	bool BuildRuntimeExecutable(RuntimeExportTarget target)
	{
		std::filesystem::path repositoryRoot = FindRepositoryRootFrom(std::filesystem::current_path());
		if (repositoryRoot.empty())
		{
			if (Ref<Project> activeProject = Project::GetActive())
				repositoryRoot = FindRepositoryRootFrom(activeProject->GetProjectDirectory());
		}

		if (repositoryRoot.empty())
		{
			LUX_CONSOLE_LOG_ERROR("Could not locate repository root for Lux-Runtime build.");
			return false;
		}

#ifdef LUX_PLATFORM_LINUX
		std::string config = RuntimeExportTargetToString(target);
		std::transform(config.begin(), config.end(), config.begin(), ::tolower);
		const std::string command =
			"make -C \"" + (repositoryRoot / "Lux-Runtime").string() + "\""
			+ " -f Makefile config=" + config;
#else
		const std::filesystem::path projectFile = repositoryRoot / "Lux-Runtime" / "Lux-Runtime.vcxproj";
		if (!FileExists(projectFile))
		{
			LUX_CONSOLE_LOG_ERROR("Lux-Runtime project file not found: {}", projectFile.string());
			return false;
		}

		auto QuotePowerShellArgument = [](std::string value) -> std::string
		{
			std::string result = "'";
			for (char c : value)
			{
				if (c == '\'')
					result += "''";
				else
					result += c;
			}
			result += "'";
			return result;
		};

		const std::filesystem::path msbuildPath = "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe";
		const std::string msbuild = FileExists(msbuildPath) ? msbuildPath.string() : "MSBuild.exe";
		const std::string command =
			"powershell -NoProfile -ExecutionPolicy Bypass -Command \"& "
			+ QuotePowerShellArgument(msbuild) + " "
			+ QuotePowerShellArgument(projectFile.string())
			+ " /t:Build /p:Configuration=" + RuntimeExportTargetToString(target)
			+ " /p:Platform=x64 /m:1 /nr:false /v:minimal\"";
#endif

		LUX_CONSOLE_LOG_INFO("Building Lux-Runtime ({})...", RuntimeExportTargetToString(target));
		const int result = std::system(command.c_str());
		if (result != 0)
		{
			LUX_CONSOLE_LOG_ERROR("Lux-Runtime build failed with exit code {}.", result);
			return false;
		}

		LUX_CONSOLE_LOG_INFO("Lux-Runtime build complete.");
		return true;
	}

	std::filesystem::path ResolveScriptProjectFile(Ref<Project> project)
	{
		if (!project)
			return {};

		std::filesystem::path scriptProject = project->GetScriptProjectPath();
		if (FileExists(scriptProject))
			return scriptProject;

		std::filesystem::path scriptProjectFilename = project->GetConfig().ScriptModulePath.filename();
		if (!scriptProjectFilename.empty())
		{
			scriptProjectFilename.replace_extension(".csproj");
			scriptProject = project->GetAssetDirectory() / "Scripts" / scriptProjectFilename;
		}

		return scriptProject;
	}

	bool IsScriptModuleOutdated(const std::filesystem::path& scriptModule, const std::filesystem::path& scriptProject)
	{
		std::error_code ec;
		if (!FileExists(scriptModule))
			return true;
		if (!FileExists(scriptProject))
			return false;

		const auto moduleWriteTime = std::filesystem::last_write_time(scriptModule, ec);
		if (ec)
			return true;

		ec.clear();
		if (std::filesystem::last_write_time(scriptProject, ec) > moduleWriteTime && !ec)
			return true;

		const std::filesystem::path scriptsDirectory = scriptProject.parent_path();
		if (!std::filesystem::exists(scriptsDirectory, ec))
			return false;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(scriptsDirectory, ec))
		{
			if (ec)
				break;
			if (!entry.is_regular_file(ec))
				continue;

			const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), scriptsDirectory, ec);
			if (!ec && !relativePath.empty())
			{
				const std::filesystem::path first = *relativePath.begin();
				if (first == "Binaries" || first == "Intermediates")
					continue;
			}

			const std::filesystem::path extension = entry.path().extension();
			if (extension != ".cs" && extension != ".csproj" && extension != ".props" && extension != ".targets" && extension != ".lua")
				continue;

			ec.clear();
			if (entry.last_write_time(ec) > moduleWriteTime && !ec)
				return true;
		}

		return false;
	}

	bool BuildScriptModule(RuntimeExportTarget target)
	{
		Ref<Project> project = Project::GetActive();
		if (!project)
		{
			LUX_CONSOLE_LOG_ERROR("No active project to build scripts for.");
			return false;
		}

		const std::filesystem::path scriptProject = ResolveScriptProjectFile(project);
		if (!FileExists(scriptProject))
		{
			LUX_CONSOLE_LOG_ERROR("Script project file not found: {}", scriptProject.string());
			return false;
		}

		if (!ScriptBuilder::BuildProject(scriptProject, RuntimeExportTargetToString(target)))
			return false;

		const std::filesystem::path scriptModule = project->GetScriptModuleFilePath();
		if (!FileExists(scriptModule))
		{
			LUX_CONSOLE_LOG_ERROR("Script build completed, but the script module was not found: {}", scriptModule.string());
			return false;
		}

		LUX_CONSOLE_LOG_INFO("Script build complete: {}", scriptModule.string());
		return true;
	}

}
