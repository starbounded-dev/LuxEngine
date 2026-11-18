// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "RuntimeExportUtils.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/Assert.h"
#include "Lux/Core/Log.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Utilities/FileSystem.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <tuple>
#include <format>
#include <fstream>

namespace Lux::RuntimeExport {

	bool ShipsLibrary(const std::string& filename, RuntimeExportTarget target)
	{
		if (filename.find("vaudionative") != std::string::npos)
			return false;
		const auto named = [&filename](std::string_view name) { return filename.starts_with(name) || filename.starts_with(std::string("lib") + std::string(name)); };
		if (named("assimp"))
			return false;
		if (target == RuntimeExportTarget::Dist && (named("shaderc") || named("dxcompiler")))
			return false;
		// FMOD's, left in the shared output folder by an FMOD build from before its removal.
		return !filename.starts_with("fmod") && !filename.starts_with("libfmod");
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

	bool CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination, bool skipDebugFiles,
		std::initializer_list<std::string_view> skip)
	{
		std::error_code ec;
		std::filesystem::recursive_directory_iterator it(source, ec), end;
		while (!ec && it != end)
		{
			const auto& entry = *it;
			const auto relativePath = entry.path().lexically_relative(source);
			const auto first = relativePath.begin()->string();
			if (first == "Cache" || std::ranges::find(skip, first) != skip.end())
				it.disable_recursion_pending();
			else
			{
				const auto target = destination / relativePath;
				const auto status = entry.status(ec);
				if (ec)
					break;
				if (std::filesystem::is_directory(status))
				{
					std::filesystem::create_directories(target, ec);
					if (ec)
					{
						LUX_CORE_ERROR_TAG("Project", "Cannot create export directory '{}': {}", target.string(), ec.message());
						return false;
					}
				}
				else if (std::filesystem::is_regular_file(status))
				{
					const auto extension = entry.path().extension();
					const bool debugFile = extension == ".pdb" || extension == ".ilk" || extension == ".exp";
					if ((!skipDebugFiles || !debugFile) && !CopyFileIfExists(entry.path(), target, true))
						return false;
				}
			}
			it.increment(ec);
		}
		if (ec)
		{
			LUX_CORE_ERROR_TAG("Project", "Cannot read export directory '{}': {}", source.string(), ec.message());
			return false;
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

	std::filesystem::path FindRepositoryRoot(const std::filesystem::path& projectDirectory)
	{
		std::error_code ec;
		std::filesystem::path root = FindRepositoryRootFrom(projectDirectory);
		if (root.empty())
			root = FindRepositoryRootFrom(std::filesystem::current_path(ec));
		return root;
	}

	std::filesystem::path FindEditorDirectory(std::string_view name, const std::filesystem::path& projectDirectory)
	{
		std::error_code ec;
		const std::filesystem::path current = std::filesystem::current_path(ec);
		const std::filesystem::path root = FindRepositoryRoot(projectDirectory);
		const std::filesystem::path folder(name);
		return FindFirstExistingDirectory({
			current / folder,
			current / ".." / "Editor" / folder,
			root / "Editor" / folder,
			std::filesystem::path("Editor") / folder
		});
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

	std::filesystem::path GetPrebuiltRuntimeDirectory(RuntimeExportTarget target)
	{
		std::error_code ec;
		return std::filesystem::current_path(ec) / "Runtime" / RuntimeExportTargetToString(target);
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

		// Last: a source build beats the installed player, because it is the one a developer just changed.
		candidates.emplace_back((GetPrebuiltRuntimeDirectory(target) / RuntimeExeName).lexically_normal());

		for (const std::filesystem::path& candidate : candidates)
		{
			if (FileExists(candidate))
				return candidate;
		}

		return {};
	}

	std::optional<RuntimeExportTarget> ResolveExportTarget(RuntimeExportTarget preferred, const std::filesystem::path& projectDirectory)
	{
		if (!GetRuntimeExecutablePath(preferred).empty() || !FindRepositoryRoot(projectDirectory).empty())
			return preferred;

		for (RuntimeExportTarget target : { RuntimeExportTarget::Dist, RuntimeExportTarget::Release, RuntimeExportTarget::Debug })
			if (!GetRuntimeExecutablePath(target).empty())
				return target;
		return std::nullopt;
	}

	bool IsRuntimeExecutableOutdated(const std::filesystem::path& runtimeExe, const std::filesystem::path& repositoryRoot)
	{
		std::error_code ec;
		if (runtimeExe.empty() || !std::filesystem::exists(runtimeExe, ec))
			return true;

		const auto executableWriteTime = std::filesystem::last_write_time(runtimeExe, ec);
		if (ec)
			return true;

		const std::array<std::filesystem::path, 3> sourceRoots = {
			repositoryRoot / "Lux-Runtime",
			repositoryRoot / "Core" / "Source",
			repositoryRoot / "Core" / "vendor" / "soundwave"
		};
		// Soundwave is linked into the executable. A build-input change must invalidate an existing
		// runtime just as an engine source change does.
		for (const auto& relative : { "Dependencies.lua", "premake5.lua", "Core/premake5.lua" })
		{
			const auto inputTime = std::filesystem::last_write_time(repositoryRoot / relative, ec);
			if (!ec && inputTime > executableWriteTime)
				return true;
			ec.clear();
		}

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
				if (extension != ".cpp" && extension != ".h" && extension != ".hpp" && extension != ".inc" && extension != ".c" && extension != ".rc" && extension != ".lua")
					continue;

				if (entry.last_write_time(ec) > executableWriteTime && !ec)
					return true;
			}
		}

		return false;
	}

	bool BuildRuntimeExecutable(RuntimeExportTarget target, const std::filesystem::path& repositoryRoot, std::string& outError)
	{
#ifdef LUX_PLATFORM_LINUX
		std::string config = RuntimeExportTargetToString(target);
		std::transform(config.begin(), config.end(), config.begin(), ::tolower);
		const std::string command =
			"make -C \"" + repositoryRoot.string() + "\""
			+ " -f Makefile config=" + config + " Lux-Runtime";
#else
		const std::filesystem::path projectFile = repositoryRoot / "Lux-Runtime" / "Lux-Runtime.vcxproj";
		if (!FileExists(projectFile))
		{
			outError = std::format("Lux-Runtime project file not found: {}", projectFile.string());
			LUX_CONSOLE_LOG_ERROR("{}", outError);
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

		// vswhere ships with every VS 2017+ installer and finds MSBuild for any edition or version;
		// PATH is the fallback.
		//
		// Building the .vcxproj outside the solution defaults $(SolutionDir) to each project's own
		// folder, and premake writes %{wks.location} as $(SolutionDir), so Core's post-build would
		// copy Coral from Core\Core\vendor. Pass the root the solution build uses. No trailing
		// backslash: generated uses are "$(SolutionDir)\...", and PowerShell quotes an argument
		// with spaces, where a final backslash would escape the closing quote.
		const std::string command =
			"powershell -NoProfile -ExecutionPolicy Bypass -Command \""
			"$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\\Installer\\vswhere.exe'; "
			"$msbuild = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\\**\\Bin\\MSBuild.exe' | Select-Object -First 1 }; "
			"if (-not $msbuild) { $msbuild = 'MSBuild.exe' }; "
			"& $msbuild "
			+ QuotePowerShellArgument(projectFile.string())
			+ " /t:Build /p:Configuration=" + RuntimeExportTargetToString(target)
			+ " /p:Platform=x64 " + QuotePowerShellArgument("/p:SolutionDir=" + repositoryRoot.string())
			+ " /m:1 /nr:false /v:minimal\"";
#endif

		LUX_CONSOLE_LOG_INFO("Building Lux-Runtime ({})...", RuntimeExportTargetToString(target));
		const int result = std::system(command.c_str());
		if (result != 0)
		{
			LUX_CONSOLE_LOG_ERROR("Lux-Runtime build failed with exit code {}.", result);
			outError = std::format("Lux-Runtime ({}) build failed with exit code {}. The compiler output is in the terminal, not the Log panel.",
				RuntimeExportTargetToString(target), result);
			return false;
		}

		LUX_CONSOLE_LOG_INFO("Lux-Runtime build complete.");
		return true;
	}

	std::filesystem::path FindDotNetRoot()
	{
		std::error_code ec;
		std::vector<std::filesystem::path> candidates;
		for (const char* variable : { "DOTNET_ROOT", "DOTNET_ROOT_X64" })
			if (const char* value = std::getenv(variable); value && *value)
				candidates.emplace_back(value);
#ifdef LUX_PLATFORM_LINUX
		for (const char* path : { "/usr/share/dotnet", "/usr/lib/dotnet", "/usr/lib64/dotnet", "/opt/dotnet" })
			candidates.emplace_back(path);
		if (const char* home = std::getenv("HOME"); home && *home)
			candidates.emplace_back(std::filesystem::path(home) / ".dotnet");
#else
		if (const char* programFiles = std::getenv("ProgramFiles"); programFiles && *programFiles)
			candidates.emplace_back(std::filesystem::path(programFiles) / "dotnet");
#endif
		for (const std::filesystem::path& candidate : candidates)
			if (std::filesystem::is_directory(candidate / "shared" / "Microsoft.NETCore.App", ec))
				return candidate;
		return {};
	}

	std::filesystem::path FindNewestVersionDirectory(const std::filesystem::path& directory, uint32_t minimumMajor)
	{
		// "10.0.12" -> {10, 0, 12}, stable. "10.0.0-rc.1" is the same numbers, not stable, so it ranks below 10.0.0.
		const auto parse = [](const std::string& name, std::array<uint32_t, 3>& numbers, bool& stable)
		{
			numbers = {};
			size_t part = 0, i = 0;
			while (part < numbers.size() && i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])))
			{
				uint64_t value = 0;
				for (; i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])); ++i)
					value = std::min<uint64_t>(value * 10 + static_cast<uint64_t>(name[i] - '0'), 0xFFFFFFFFu);
				numbers[part++] = static_cast<uint32_t>(value);
				if (i < name.size() && name[i] == '.')
					++i;
				else
					break;
			}
			stable = i == name.size();
			return part > 0;
		};

		std::error_code ec;
		std::filesystem::path best;
		std::array<uint32_t, 3> bestNumbers = {};
		bool bestStable = false;
		std::string bestName;
		// A stable release always wins over a newer preview: a game ships what its makers run, not a beta.
		for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_directory(ec))
				continue;
			std::array<uint32_t, 3> numbers;
			bool stable = false;
			if (!parse(it->path().filename().string(), numbers, stable) || numbers[0] < minimumMajor)
				continue;
			// Equal numbers and both previews: the later name (rc over preview) wins.
			const std::string name = it->path().filename().string();
			if (best.empty() || std::tie(stable, numbers, name) > std::tie(bestStable, bestNumbers, bestName))
			{
				best = it->path();
				bestName = name;
				bestNumbers = numbers;
				bestStable = stable;
			}
		}
		return best;
	}

	bool BundleDotNetRuntime(const std::filesystem::path& dotNetRoot, const std::filesystem::path& destination, std::string& outError)
	{
		// Coral.Managed targets net10.0 and rolls forward, so any installed major from 10 on runs it.
		constexpr uint32_t k_MinimumMajor = 10;
		const std::filesystem::path framework = FindNewestVersionDirectory(dotNetRoot / "shared" / "Microsoft.NETCore.App", k_MinimumMajor);
		const std::filesystem::path hostfxr = FindNewestVersionDirectory(dotNetRoot / "host" / "fxr", k_MinimumMajor);
		if (framework.empty() || hostfxr.empty())
		{
			outError = std::format("No .NET {} runtime found under '{}'. Install the .NET {} SDK, or export without scripts.",
				k_MinimumMajor, dotNetRoot.string(), k_MinimumMajor);
			return false;
		}

		std::error_code ec;
		std::filesystem::remove_all(destination, ec);
		const std::filesystem::path frameworkDestination = destination / "shared" / "Microsoft.NETCore.App" / framework.filename();
		const std::filesystem::path hostfxrDestination = destination / "host" / "fxr" / hostfxr.filename();
		if (!CopyDirectoryRecursive(framework, frameworkDestination) || !CopyDirectoryRecursive(hostfxr, hostfxrDestination))
		{
			outError = std::format("Copying the .NET runtime from '{}' failed. The Log panel has the reason.", dotNetRoot.string());
			return false;
		}

		LUX_CONSOLE_LOG_INFO("  .NET runtime {}: bundled", framework.filename().string());
		return true;
	}

	bool LaunchExportedGame(const std::filesystem::path& executable)
	{
		if (!FileExists(executable))
			return false;
#ifdef LUX_PLATFORM_LINUX
		// Its own session and no terminal, so closing the editor does not end the game. QuoteShellArgument keeps
		// a file name from running a command.
		const std::string command = "setsid " + FileSystem::QuoteShellArgument(std::filesystem::absolute(executable)) + " >/dev/null 2>&1 &";
		return std::system(command.c_str()) == 0;
#else
		// ShellExecute "open" runs the executable; the player sets its own working directory.
		return FileSystem::OpenExternally(executable);
#endif
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

	bool BuildScriptModule(RuntimeExportTarget target, const std::filesystem::path& scriptProject, const std::filesystem::path& scriptModule, std::string& outError)
	{
		if (!ScriptBuilder::BuildProject(scriptProject, RuntimeExportTargetToString(target)))
		{
			outError = std::format("Script build ({}) failed. The dotnet output is in the terminal, not the Log panel.", RuntimeExportTargetToString(target));
			return false;
		}

		if (!FileExists(scriptModule))
		{
			outError = std::format("Script build completed, but the script module was not found: {}", scriptModule.string());
			LUX_CONSOLE_LOG_ERROR("{}", outError);
			return false;
		}

		LUX_CONSOLE_LOG_INFO("Script build complete: {}", scriptModule.string());
		return true;
	}

	RuntimeBuildJob::RuntimeBuildJob()
		: m_Thread("Runtime Build")
	{
	}

	RuntimeBuildJob::~RuntimeBuildJob()
	{
		// A joinable std::thread terminates the process when destroyed.
		m_Thread.Join();
	}

	bool RuntimeBuildJob::Start(RuntimeExportTarget target, bool buildRuntime, bool buildScripts)
	{
		LUX_CORE_ASSERT(buildRuntime || buildScripts, "RuntimeBuildJob::Start with nothing to build");

		auto fail = [this](std::string message)
		{
			LUX_CONSOLE_LOG_ERROR("{}", message);
			m_LastError = std::move(message);
			return false;
		};

		if (m_Running)
			return fail("A Lux-Runtime or script build is already running.");

		std::filesystem::path repositoryRoot;
		if (buildRuntime)
		{
			repositoryRoot = FindRepositoryRootFrom(std::filesystem::current_path());
			if (repositoryRoot.empty())
			{
				if (Ref<Project> activeProject = Project::GetActive())
					repositoryRoot = FindRepositoryRootFrom(activeProject->GetProjectDirectory());
			}

			if (repositoryRoot.empty())
				return fail("Could not locate repository root for Lux-Runtime build.");
		}

		std::filesystem::path scriptProject;
		std::filesystem::path scriptModule;
		if (buildScripts)
		{
			Ref<Project> project = Project::GetActive();
			if (!project)
				return fail("No active project to build scripts for.");

			scriptProject = ResolveScriptProjectFile(project);
			if (!FileExists(scriptProject))
				return fail(std::format("Script project file not found: {}", scriptProject.string()));
			scriptModule = project->GetScriptModuleFilePath();
		}

		m_Target = target;
		m_RepositoryRoot = std::move(repositoryRoot);
		m_ScriptProject = std::move(scriptProject);
		m_ScriptModule = std::move(scriptModule);
		m_Result = {};
		m_LastError.clear();
		m_Step = buildRuntime ? Step::Runtime : Step::Scripts;
		m_Finished = false;
		m_Running = true;
		m_StartTime = std::chrono::steady_clock::now();

		// Everything the worker reads is written above, before the thread starts, and stays unchanged
		// until Poll or Wait joins it.
		m_Thread.Dispatch([this]()
		{
			LUX_PROFILE_THREAD("Runtime Build");

			RuntimeBuildResult result;
			if (!m_RepositoryRoot.empty())
				result.RuntimeFailed = !BuildRuntimeExecutable(m_Target, m_RepositoryRoot, result.Error);

			// An export stops on a failed runtime build, so its scripts are not built after one.
			if (!result.RuntimeFailed && !m_ScriptProject.empty())
			{
				m_Step = Step::Scripts;
				result.ScriptsFailed = !BuildScriptModule(m_Target, m_ScriptProject, m_ScriptModule, result.Error);
			}

			m_Result = std::move(result);
			m_Finished.store(true, std::memory_order_release);
		});
		return true;
	}

	std::optional<RuntimeBuildResult> RuntimeBuildJob::Poll()
	{
		if (!m_Running || !m_Finished.load(std::memory_order_acquire))
			return std::nullopt;

		m_Thread.Join();
		m_Running = false;
		m_LastError = m_Result.Error;
		return std::move(m_Result);
	}

	void RuntimeBuildJob::Wait()
	{
		if (!m_Running)
			return;

		if (!m_Finished.load(std::memory_order_acquire))
			LUX_CONSOLE_LOG_INFO("Waiting for the running Lux-Runtime or script build to finish...");
		m_Thread.Join();
		m_Running = false;
	}

	std::string RuntimeBuildJob::GetStatusText() const
	{
		if (!m_Running)
			return {};

		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_StartTime).count();
		return std::format("Building {} ({})...  {}:{:02}", m_Step == Step::Runtime ? "Lux-Runtime" : "scripts",
			RuntimeExportTargetToString(m_Target), seconds / 60, seconds % 60);
	}

}
